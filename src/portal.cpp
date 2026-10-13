#include "portal.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>

#include "settings.h"
#include "quotes.h"
#include "alarm.h"
#include "screen.h"
#include "epd397.h"
#include "storage.h"
#include "market.h"
#include "firmware.h"
#include "portal_html.h"

static WebServer server(80);
static DNSServer dns;
static bool inApMode = false;
static String hotspotSsid;
static uint32_t restartAt = 0;

static void sendJson(int code, const JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void sendError(int code, const String &msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  sendJson(code, doc);
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", PORTAL_HTML);
}

static void handleGetConfig() {
  JsonDocument doc;
  settingsToJson(doc, false);
  sendJson(200, doc);
}

static void handleStatus() {
  JsonDocument doc;
  doc["ap"] = inApMode;
  doc["apSsid"] = hotspotSsid;
  doc["sta"] = WiFi.status() == WL_CONNECTED;
  doc["staSsid"] = WiFi.SSID();
  doc["ip"] = WiFi.localIP().toString();
  sendJson(200, doc);
}

static void handlePostConfig() {
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) return sendError(400, "Invalid request");
  Settings next;
  String err = settingsFromJson(in.as<JsonVariantConst>(), next);
  if (!err.isEmpty()) return sendError(400, err);
  settings = next;
  if (!settingsSave()) return sendError(500, "Could not save settings");
  settingsBackupToSd();  // keep the card copy current (no-op without a card)
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
  restartAt = millis() + 1500;  // let the response reach the browser first
}

static void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (JsonVariant v : arr) dup |= v["ssid"] == ssid;
    if (dup) continue;  // mesh networks show up once per access point
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  sendJson(200, doc);
}

static void handleLookup() {
  String sym = server.arg("symbol");
  if (!normaliseSymbol(sym)) return sendError(200, "Invalid symbol");
  if (WiFi.status() != WL_CONNECTED) return sendError(200, "Ticker is offline; it will check the symbol after restart");
  String name;
  float price = 0;
  String err = lookupSymbol(sym, name, price);
  if (!err.isEmpty()) return sendError(200, err);
  JsonDocument doc;
  doc["ok"] = true;
  doc["name"] = name;
  doc["price"] = price;
  sendJson(200, doc);
}

static void handleTestAlarm() {
  int vol = server.hasArg("volume") ? server.arg("volume").toInt() : settings.alarmVolume;
  alarmRing(3, vol);
  server.send(204);
}

// Current screen as a PBM image (black bit = 1, so invert the panel's format).
static void handleScreenshot() {
  const uint8_t *buf = screenBuffer();
  String header = "P4\n" + String(Epd397::WIDTH) + " " + String(Epd397::HEIGHT) + "\n";
  server.setContentLength(header.length() + Epd397::FRAME_BYTES);
  server.send(200, "image/x-portable-bitmap", "");
  server.sendContent(header);
  uint8_t chunk[800];
  for (size_t off = 0; off < Epd397::FRAME_BYTES; off += sizeof(chunk)) {
    size_t n = min(sizeof(chunk), Epd397::FRAME_BYTES - off);
    for (size_t i = 0; i < n; i++) chunk[i] = ~buf[off + i];
    server.sendContent((const char *)chunk, n);
  }
}

static void handleSetupMode() {
  server.send(204);
  delay(100);
  restartIntoSetupMode();
}

// ---- SD card

static void handleSdStatus() {
  JsonDocument doc;
  doc["version"] = FW_VERSION;
  doc["present"] = sdReady();
  doc["totalMB"] = (uint32_t)(sdTotalBytes() >> 20);
  doc["usedMB"] = (uint32_t)(sdUsedBytes() >> 20);
  doc["backup"] = sdExists(SD_SETTINGS_BACKUP);
  doc["firmware"] = firmwarePendingOnSd() ? (int)sdFileSize(SD_FIRMWARE_FILE) : 0;
  JsonArray sounds = doc["sounds"].to<JsonArray>();
  String names[24];
  int n = sdListSounds(names, 24);
  for (int i = 0; i < n; i++) sounds.add(names[i]);
  doc["holidaySource"] = holidaysSource();
  sendJson(200, doc);
}

static void handleSdBackup() {
  if (!sdReady()) return sendError(200, "No SD card inserted");
  if (!settingsBackupToSd()) return sendError(200, "Could not write to the SD card");
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

static void handleSdRestore() {
  if (!sdExists(SD_SETTINGS_BACKUP)) return sendError(200, "No backup found on the SD card");
  String err = settingsRestoreFromSd(SD_SETTINGS_BACKUP);
  if (!err.isEmpty()) return sendError(200, err);
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
  restartAt = millis() + 1500;
}

static void handleSdPlay() {
  String name = server.arg("file");
  if (name.isEmpty() || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) return sendError(400, "Bad file name");
  String path = String(SD_SOUNDS_DIR "/") + name;
  if (!sdExists(path)) return sendError(404, "Not found");
  alarmRing(5, settings.alarmVolume, path);
  server.send(204);
}

static void handleSdDeleteSound() {
  String name = server.arg("file");
  if (name.isEmpty() || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) return sendError(400, "Bad file name");
  String path = String(SD_SOUNDS_DIR "/") + name;
  if (!sdExists(path) || !SD_MMC.remove(path)) return sendError(200, "Could not delete " + name);
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

static void handleInstallFirmware() {
  if (!firmwarePendingOnSd()) return sendError(200, "No firmware.bin on the SD card");
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
  restartAt = millis() + 1500;  // installed at power-on
}

// Browser upload to the card: ?kind=sound (any .wav into /ticker/sounds) or
// ?kind=firmware (saved as /ticker/firmware.bin, installed at next power-on).
static File uploadFile;
static String uploadPath, uploadError;

static void handleUploadData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadError = "";
    String kind = server.arg("kind");
    String name = up.filename;
    name = name.substring(name.lastIndexOf('/') + 1);
    name = name.substring(name.lastIndexOf('\\') + 1);
    String lower = name;
    lower.toLowerCase();
    if (!sdReady()) uploadError = "No SD card inserted";
    else if (kind == "sound") {
      bool okName = lower.endsWith(".wav") && name.length() <= 40;
      for (char c : name) okName &= isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '&';
      if (!okName) uploadError = "Sound files must be .wav with a simple name (letters, digits, - _)";
      uploadPath = String(SD_SOUNDS_DIR "/") + name;
    } else if (kind == "firmware") {
      if (!lower.endsWith(".bin")) uploadError = "Firmware must be a .bin file";
      uploadPath = SD_FIRMWARE_FILE;
    } else {
      uploadError = "Unknown upload type";
    }
    if (uploadError.isEmpty()) {
      uploadFile = SD_MMC.open(uploadPath + ".part", FILE_WRITE);
      if (!uploadFile) uploadError = "Could not create the file on the SD card";
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile && uploadFile.write(up.buf, up.currentSize) != up.currentSize) {
      uploadError = "SD card write failed (card full?)";
      uploadFile.close();
    }
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (up.status == UPLOAD_FILE_ABORTED && uploadError.isEmpty()) uploadError = "Upload interrupted";
    if (uploadError.isEmpty()) {
      if (!sdRename(uploadPath + ".part", uploadPath)) uploadError = "Could not save the file";
    } else if (sdReady()) {
      SD_MMC.remove(uploadPath + ".part");
    }
  }
}

static void handleUploadDone() {
  if (!uploadError.isEmpty()) return sendError(200, uploadError);
  JsonDocument doc;
  doc["ok"] = true;
  doc["path"] = uploadPath;
  sendJson(200, doc);
}

static void handleGetHolidays() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", holidaysText());
}

static void handlePostHolidays() {
  String err = holidaysSave(server.arg("plain"));
  if (!err.isEmpty()) return sendError(200, err);
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

static void handleNotFound() {
  if (inApMode) {
    // Captive portal: send every unknown URL (OS connectivity checks included) to the page.
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

void portalBegin(bool apMode, const String &apSsid) {
  inApMode = apMode;
  hotspotSsid = apSsid;
  if (apMode) dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/lookup", HTTP_GET, handleLookup);
  server.on("/api/test-alarm", HTTP_POST, handleTestAlarm);
  server.on("/screenshot.pbm", HTTP_GET, handleScreenshot);
  server.on("/api/setup-mode", HTTP_POST, handleSetupMode);
  server.on("/api/sd", HTTP_GET, handleSdStatus);
  server.on("/api/sd/backup", HTTP_POST, handleSdBackup);
  server.on("/api/sd/restore", HTTP_POST, handleSdRestore);
  server.on("/api/sd/play", HTTP_POST, handleSdPlay);
  server.on("/api/sd/delete-sound", HTTP_POST, handleSdDeleteSound);
  server.on("/api/sd/install-firmware", HTTP_POST, handleInstallFirmware);
  server.on("/api/sd/upload", HTTP_POST, handleUploadDone, handleUploadData);
  server.on("/api/holidays", HTTP_GET, handleGetHolidays);
  server.on("/api/holidays", HTTP_POST, handlePostHolidays);
  server.onNotFound(handleNotFound);
  server.begin();
}

void portalLoop() {
  if (inApMode) dns.processNextRequest();
  server.handleClient();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
}
