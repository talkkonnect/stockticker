// SET stock ticker for the Waveshare ESP32-S3-ePaper-3.97 (800x480, SSD1677).
//
// Normal mode: joins one of up to 4 saved Wi-Fi networks, fetches up to 3 SET
// stocks from Yahoo Finance, shows price/chart/positions with long & short P/L,
// and rings the speaker when a position's target price is reached. The settings
// page is served at the board's IP address.
//
// Setup mode: the board runs its own Wi-Fi hotspot with a captive settings
// portal. Entered by holding MENU for 3 s (or holding MENU/BOOT at power-on),
// or automatically when no network is configured or none can be joined.
//
// Buttons: UP / DOWN switch stock page, MENU silences the alarm, MENU held 3 s
// enters setup mode. Any button silences a ringing alarm.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>

#include "settings.h"
#include "quotes.h"
#include "portfolio.h"
#include "screen.h"
#include "portal.h"
#include "alarm.h"
#include "storage.h"
#include "market.h"
#include "firmware.h"

constexpr int BTN_UP = 4, BTN_MENU = 5, BTN_DOWN = 6, BTN_BOOT = 0;  // active low
constexpr uint32_t LONG_PRESS_MS = 3000;
constexpr uint32_t SETUP_RETRY_MS = 10 * 60 * 1000;  // auto-setup retries Wi-Fi after 10 min

// Survives ESP.restart() so MENU-long-press can reboot straight into setup mode.
RTC_NOINIT_ATTR static uint32_t bootIntoSetup;
constexpr uint32_t SETUP_MAGIC = 0x5E7A9001;

static WiFiMulti wifiMulti;
static bool setupMode = false;
static bool setupAuto = false;  // entered because Wi-Fi failed, not by the user
static uint32_t setupStarted = 0;

static int focus = 0;
static bool hit[MAX_STOCKS][MAX_POSITIONS] = {};
static time_t lastUpdate = 0;
static uint32_t lastFetch = 0, lastRotate = 0;
static bool firstFetchDone = false;

// ---------------------------------------------------------------- buttons

struct Button {
  int pin;
  bool down = false;
  bool longFired = false;
  uint32_t since = 0;
};

static Button buttons[] = {{BTN_UP}, {BTN_MENU}, {BTN_DOWN}};

enum ButtonEvent { NONE, CLICK, LONG };

static ButtonEvent pollButton(Button &b) {
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (pressed && !b.down) {
    b.down = true;
    b.longFired = false;
    b.since = now;
  } else if (pressed && b.down && !b.longFired && now - b.since >= LONG_PRESS_MS) {
    b.longFired = true;
    return LONG;
  } else if (!pressed && b.down) {
    b.down = false;
    if (!b.longFired && now - b.since >= 30) return CLICK;  // 30 ms debounce
  }
  return NONE;
}

// ---------------------------------------------------------------- modes

static String hotspotName() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[32];
  snprintf(buf, sizeof(buf), "StockTicker-%02X%02X", mac[4], mac[5]);
  return buf;
}

static void enterSetupMode(const String &note, bool automatic) {
  setupMode = true;
  setupAuto = automatic;
  setupStarted = millis();
  Serial.printf("Setup mode: %s\n", note.c_str());

  // Keep the station link if we have one so the portal can check symbols online.
  WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_AP_STA : WIFI_AP);
  String ssid = hotspotName();
  WiFi.softAP(ssid.c_str(), settings.apPass.c_str());
  delay(200);
  String ip = WiFi.softAPIP().toString();
  portalBegin(true, ssid);

  drawSetup(ssid, settings.apPass, ip, note);
  screenPush(true);
}

static bool connectWiFi(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (wifiMulti.run(5000) == WL_CONNECTED) {
      Serial.printf("Wi-Fi connected to %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
  }
  return false;
}

static void startNormalMode() {
  for (int i = 0; i < settings.nWifi; i++)
    wifiMulti.addAP(settings.wifi[i].ssid.c_str(), settings.wifi[i].pass.c_str());
  WiFi.mode(WIFI_STA);

  String names;
  for (int i = 0; i < settings.nWifi; i++) names += (i ? ", " : "") + settings.wifi[i].ssid;
  drawMessage("Connecting to Wi-Fi...", names);
  screenPush(true);

  if (!connectWiFi(45000)) {
    enterSetupMode("Could not connect to: " + names, true);
    return;
  }
  configTzTime("ICT-7", "pool.ntp.org", "time.google.com");
  for (int i = 0; i < 20 && time(nullptr) < 1700000000; i++) delay(250);
  portalBegin(false, "");
}

void restartIntoSetupMode() {
  bootIntoSetup = SETUP_MAGIC;
  drawMessage("Starting setup mode...", "");
  screenPush(true);
  ESP.restart();
}

// ---------------------------------------------------------------- ticker

static void redraw() {
  drawTicker(focus, hit, alarmRinging(), WiFi.localIP().toString(), lastUpdate);
  screenPush();
}

static void fetchAll() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi(10000);
  for (int s = 0; s < settings.nStocks; s++) {
    if (fetchQuote(settings.stocks[s].symbol, quotes[s]))
      Serial.printf("%s %.2f (prev %.2f), %d points\n", settings.stocks[s].symbol.c_str(), quotes[s].price,
                    quotes[s].prevClose, quotes[s].n);
    else
      Serial.printf("%s fetch failed: %s\n", settings.stocks[s].symbol.c_str(), quotes[s].error.c_str());
    portalLoop();  // stay responsive between the slow HTTPS calls
  }
  if (fetchSetIndex()) Serial.printf("SET index %.2f (prev %.2f)\n", setIndex.price, setIndex.prevClose);
  else Serial.printf("SET index fetch failed: %s\n", setIndex.error.c_str());
  portalLoop();
  lastUpdate = time(nullptr);

  // Ring once each time a position crosses into its target.
  int ringFor = -1;
  String ringSound;
  for (int s = 0; s < settings.nStocks; s++) {
    if (!quotes[s].valid || !quotes[s].error.isEmpty()) continue;
    for (int p = 0; p < settings.stocks[s].nPos; p++) {
      bool now = targetReached(settings.stocks[s].pos[p], quotes[s].price);
      if (now && !hit[s][p]) {
        const Position &pos = settings.stocks[s].pos[p];
        Serial.printf("TARGET: %s %s #%d at %.2f (target %.2f)\n", settings.stocks[s].symbol.c_str(),
                      pos.isShort ? "short" : "long", p + 1, quotes[s].price, pos.target);
        if (ringFor < 0) {
          ringFor = s;
          ringSound = findAlarmSound(settings.stocks[s].symbol, pos.isShort);
        }
      }
      hit[s][p] = now;
    }
  }
  if (ringFor >= 0) {
    focus = ringFor;
    lastRotate = millis();
    if (settings.alarmOn) alarmRing(settings.alarmSeconds, settings.alarmVolume, ringSound);
  }
}

static void changeFocus(int delta) {
  if (settings.nStocks < 2) return;
  focus = (focus + delta + settings.nStocks) % settings.nStocks;
  lastRotate = millis();
  redraw();
}

// ---------------------------------------------------------------- SD card at power-on

static void showFirmwareProgress(int pct) {
  drawMessage("Updating firmware from SD card...", String(pct) + "%  - do not power off");
  screenPush(pct == 0);
}

// Installs /ticker/firmware.bin and applies /ticker/restore.json if present.
static void bootFromSd() {
  if (!sdReady()) return;
  if (firmwarePendingOnSd()) {
    Serial.println("Installing firmware from SD card");
    showFirmwareProgress(0);
    String err = firmwareInstallFromSd(showFirmwareProgress);
    if (err.isEmpty()) {
      drawMessage("Firmware updated", "Restarting...");
      screenPush(true);
      ESP.restart();
    }
    Serial.printf("Firmware update failed: %s\n", err.c_str());
    drawMessage("Firmware update failed", err);
    screenPush(true);
    delay(8000);
  }
  if (sdExists(SD_RESTORE_FILE)) {
    String err = settingsRestoreFromSd(SD_RESTORE_FILE);
    sdRename(SD_RESTORE_FILE, err.isEmpty() ? SD_DIR "/restore-applied.json" : SD_DIR "/restore-failed.json");
    Serial.printf("Restore from SD: %s\n", err.isEmpty() ? "OK" : err.c_str());
    drawMessage(err.isEmpty() ? "Settings restored from SD card" : "Settings restore failed", err);
    screenPush(true);
    delay(err.isEmpty() ? 2000 : 8000);
  }
  if (!sdExists(SD_SETTINGS_BACKUP)) settingsBackupToSd();
}

// ---------------------------------------------------------------- main

void setup() {
  Serial.begin(115200);
  delay(300);
  for (Button &b : buttons) pinMode(b.pin, INPUT_PULLUP);
  pinMode(BTN_BOOT, INPUT_PULLUP);

  settingsLoad();
  screenBegin();
  sdBegin();
  bootFromSd();
  holidaysLoad();
  alarmBegin();
  WiFi.persistent(false);

  bool forced = bootIntoSetup == SETUP_MAGIC || digitalRead(BTN_MENU) == LOW || digitalRead(BTN_BOOT) == LOW;
  bootIntoSetup = 0;
  if (forced) {
    // Join a saved network first (if any) so the portal can check symbols online.
    if (settings.nWifi > 0) {
      drawMessage("Starting setup mode...", "");
      screenPush(true);
      for (int i = 0; i < settings.nWifi; i++)
        wifiMulti.addAP(settings.wifi[i].ssid.c_str(), settings.wifi[i].pass.c_str());
      WiFi.mode(WIFI_STA);
      connectWiFi(15000);
    }
    enterSetupMode("", false);
  }
  else if (settings.nWifi == 0) enterSetupMode("No Wi-Fi network is configured yet.", true);
  else startNormalMode();
}

void loop() {
  portalLoop();

  bool anyClick = false;
  ButtonEvent ev[3];
  for (int i = 0; i < 3; i++) {
    ev[i] = pollButton(buttons[i]);
    anyClick |= ev[i] == CLICK;
  }

  if (setupMode) {
    if (ev[1] == CLICK) ESP.restart();  // MENU: leave setup without changes
    if (setupAuto && settings.nWifi > 0 && millis() - setupStarted > SETUP_RETRY_MS && WiFi.softAPgetStationNum() == 0)
      ESP.restart();  // nobody is configuring; try the saved networks again
    delay(5);
    return;
  }

  // Any button silences a ringing alarm (and does nothing else).
  if (anyClick && alarmRinging()) {
    alarmSilence();
    redraw();
    return;
  }
  if (ev[1] == LONG) restartIntoSetupMode();
  if (ev[0] == CLICK) changeFocus(-1);
  if (ev[2] == CLICK) changeFocus(+1);

  uint32_t now = millis();
  if (!firstFetchDone || now - lastFetch >= (uint32_t)settings.fetchSeconds * 1000) {
    firstFetchDone = true;
    lastFetch = now;
    fetchAll();
    redraw();
    return;
  }
  if (settings.rotateSeconds > 0 && settings.nStocks > 1 && !alarmRinging() &&
      now - lastRotate >= (uint32_t)settings.rotateSeconds * 1000) {
    changeFocus(+1);
    return;
  }

  // Clear the ALARM badge once ringing stops on its own.
  static bool wasRinging = false;
  bool ringing = alarmRinging();
  if (wasRinging && !ringing) redraw();
  wasRinging = ringing;

  delay(10);
}
