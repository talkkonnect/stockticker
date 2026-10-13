#include "settings.h"

#include <Preferences.h>

#include "storage.h"

// include/config.h is optional and only supplies first-boot defaults.
#if __has_include("config.h")
#include "config.h"
#endif

Settings settings;

static void applyDefaults(Settings &s) {
  s = Settings();
#ifdef WIFI_SSID
  s.wifi[0].ssid = WIFI_SSID;
  s.wifi[0].pass = WIFI_PASSWORD;
  s.nWifi = 1;
#endif
#ifdef STOCK_LABEL
  s.stocks[0].symbol = STOCK_LABEL;
#else
  s.stocks[0].symbol = "SJWD";
#endif
  s.nStocks = 1;
}

bool normaliseSymbol(String &sym) {
  sym.trim();
  sym.toUpperCase();
  if (sym.endsWith(".BK")) sym.remove(sym.length() - 3);
  if (sym.length() == 0 || sym.length() > 12) return false;
  for (char c : sym)
    if (!isalnum((unsigned char)c) && c != '-' && c != '&' && c != '.') return false;
  return true;
}

// Prices have 2 decimals; avoid float noise like 9.349999 in JSON.
static double round2(float v) { return llround((double)v * 100.0) / 100.0; }

void settingsToJson(JsonDocument &doc, bool includeSecrets) {
  const Settings &s = settings;
  JsonArray wifi = doc["wifi"].to<JsonArray>();
  for (int i = 0; i < s.nWifi; i++) {
    JsonObject w = wifi.add<JsonObject>();
    w["ssid"] = s.wifi[i].ssid;
    if (includeSecrets) w["pass"] = s.wifi[i].pass;
    else w["hasPass"] = s.wifi[i].pass.length() > 0;
  }
  JsonArray stocks = doc["stocks"].to<JsonArray>();
  for (int i = 0; i < s.nStocks; i++) {
    JsonObject st = stocks.add<JsonObject>();
    st["symbol"] = s.stocks[i].symbol;
    JsonArray pos = st["pos"].to<JsonArray>();
    for (int j = 0; j < s.stocks[i].nPos; j++) {
      const Position &p = s.stocks[i].pos[j];
      JsonObject o = pos.add<JsonObject>();
      o["side"] = p.isShort ? "short" : "long";
      o["entry"] = round2(p.entry);
      o["target"] = round2(p.target);
      o["volume"] = p.volume;
      if (!p.ref.isEmpty()) o["ref"] = p.ref;
      if (!p.date.isEmpty()) o["date"] = p.date;
    }
  }
  doc["alarmOn"] = s.alarmOn;
  doc["alarmVolume"] = s.alarmVolume;
  doc["alarmSeconds"] = s.alarmSeconds;
  doc["fetchSeconds"] = s.fetchSeconds;
  doc["rotateSeconds"] = s.rotateSeconds;
  if (includeSecrets) doc["apPass"] = s.apPass;
}

String settingsFromJson(JsonVariantConst in, Settings &out) {
  Settings s;  // start from struct defaults, fill from input

  int n = 0;
  for (JsonVariantConst w : in["wifi"].as<JsonArrayConst>()) {
    String ssid = w["ssid"] | "";
    ssid.trim();
    if (ssid.isEmpty()) continue;
    if (n >= MAX_WIFI) return "At most 4 Wi-Fi networks";
    String pass = w["pass"] | "";
    if (pass.isEmpty() && (w["keep"] | false)) {
      for (int i = 0; i < settings.nWifi; i++)
        if (settings.wifi[i].ssid == ssid) pass = settings.wifi[i].pass;
    }
    if (pass.length() > 0 && pass.length() < 8) return "Wi-Fi password for " + ssid + " must be at least 8 characters";
    s.wifi[n].ssid = ssid;
    s.wifi[n].pass = pass;
    n++;
  }
  s.nWifi = n;

  n = 0;
  for (JsonVariantConst st : in["stocks"].as<JsonArrayConst>()) {
    String sym = st["symbol"] | "";
    sym.trim();
    if (sym.isEmpty()) continue;
    if (n >= MAX_STOCKS) return "At most 3 stocks";
    if (!normaliseSymbol(sym)) return "Invalid SET symbol: " + sym;
    StockCfg &sc = s.stocks[n];
    sc.symbol = sym;
    int np = 0, row = 0;
    for (JsonVariantConst p : st["pos"].as<JsonArrayConst>()) {
      row++;
      // Shares: sign is ignored (the side says long or short).
      long vol = labs(lround(p["volume"] | 0.0));
      float entry = p["entry"] | 0.0f;
      float target = p["target"] | 0.0f;
      String ref = p["ref"] | "";
      String date = p["date"] | "";
      ref.trim();
      date.trim();
      if (vol == 0 && entry == 0 && target == 0 && ref.isEmpty() && date.isEmpty()) continue;  // empty row
      if (np >= MAX_POSITIONS) return sym + ": at most 3 positions";
      String side = p["side"] | "long";
      bool isShort = side == "short";
      String where = sym + " position " + String(row) + (isShort ? " (Short)" : " (Long)");
      if (vol <= 0) return where + ": enter the number of shares";
      if (entry <= 0) return where + (isShort ? ": enter the price you sold at" : ": enter the price you bought at");
      if (target < 0) return where + ": target price cannot be negative";
      if (ref.length() > 20) return where + ": reference number is limited to 20 characters";
      int y, m, d;
      if (!date.isEmpty() && (date.length() != 10 || sscanf(date.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 ||
                              m > 12 || d < 1 || d > 31))
        return where + ": entry date must be YYYY-MM-DD";
      Position &pos = sc.pos[np++];
      pos.isShort = isShort;
      pos.entry = entry;
      pos.target = target;
      pos.volume = vol;
      pos.ref = ref;
      pos.date = date;
    }
    sc.nPos = np;
    n++;
  }
  if (n == 0) return "Add at least one stock";
  s.nStocks = n;

  s.alarmOn = in["alarmOn"] | true;
  s.alarmVolume = constrain((int)(in["alarmVolume"] | 70), 0, 100);
  s.alarmSeconds = constrain((int)(in["alarmSeconds"] | 30), 3, 600);
  s.fetchSeconds = constrain((int)(in["fetchSeconds"] | 60), 30, 3600);
  s.rotateSeconds = constrain((int)(in["rotateSeconds"] | 20), 0, 3600);
  String ap = in["apPass"] | "";
  if (ap.isEmpty()) ap = settings.apPass;  // blank = keep current
  if (ap.length() < 8) return "Setup hotspot password must be at least 8 characters";
  s.apPass = ap;

  out = s;
  return "";
}

void settingsLoad() {
  Preferences prefs;
  prefs.begin("ticker", true);
  String json = prefs.getString("cfg", "");
  prefs.end();

  applyDefaults(settings);
  if (json.isEmpty()) return;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  Settings loaded;
  String err = settingsFromJson(doc.as<JsonVariantConst>(), loaded);
  if (err.isEmpty()) settings = loaded;
  else Serial.printf("Stored settings rejected (%s); using defaults\n", err.c_str());
}

bool settingsSave() {
  JsonDocument doc;
  settingsToJson(doc, true);
  String json;
  serializeJson(doc, json);
  Preferences prefs;
  prefs.begin("ticker", false);
  bool ok = prefs.putString("cfg", json) == json.length();
  prefs.end();
  return ok;
}

bool settingsBackupToSd() {
  if (!sdReady()) return false;
  JsonDocument doc;
  settingsToJson(doc, true);
  doc["backupOf"] = "SET stock ticker settings";
  String json;
  serializeJsonPretty(doc, json);
  return sdWriteFile(SD_SETTINGS_BACKUP, json);
}

String settingsRestoreFromSd(const String &path) {
  if (!sdReady()) return "No SD card";
  String json;
  if (!sdReadFile(path, json)) return "Cannot read " + path;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return path + " is not valid JSON";
  Settings next;
  String err = settingsFromJson(doc.as<JsonVariantConst>(), next);
  if (!err.isEmpty()) return err;
  settings = next;
  return settingsSave() ? "" : "Could not save settings";
}
