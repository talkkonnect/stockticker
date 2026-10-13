#pragma once
// User settings, edited through the web portal and stored in NVS as JSON.

#include <Arduino.h>
#include <ArduinoJson.h>

constexpr int MAX_WIFI = 4;
constexpr int MAX_STOCKS = 3;
constexpr int MAX_POSITIONS = 3;

struct WifiNet {
  String ssid;
  String pass;
};

struct Position {
  bool isShort = false;  // false = long (bought), true = short (sold)
  float entry = 0;       // price bought at (long) / sold at (short)
  float target = 0;      // sell price (long) / buy-back price (short); 0 = none
  long volume = 0;       // shares
  String ref;            // user's reference number (web portal only)
  String date;           // entry date YYYY-MM-DD (web portal only)
};

struct StockCfg {
  String symbol;  // SET symbol without ".BK", e.g. "SJWD"
  int nPos = 0;
  Position pos[MAX_POSITIONS];
};

struct Settings {
  int nWifi = 0;
  WifiNet wifi[MAX_WIFI];
  int nStocks = 0;
  StockCfg stocks[MAX_STOCKS];
  bool alarmOn = true;
  int alarmVolume = 70;   // 0-100
  int alarmSeconds = 30;  // how long the alarm rings unless silenced
  int fetchSeconds = 60;  // quote refresh interval
  int rotateSeconds = 20; // page rotation between stocks; 0 = off
  String apPass = "stockticker";
};

extern Settings settings;

void settingsLoad();
bool settingsSave();
// toJson: passwords are omitted unless includeSecrets (portal never sees them).
void settingsToJson(JsonDocument &doc, bool includeSecrets);
// fromJson validates and normalises; returns an error message or "" on success.
// Wi-Fi entries with "keep": true and an empty password keep their stored password.
String settingsFromJson(JsonVariantConst in, Settings &out);
// SD card backup (includes Wi-Fi passwords so a restore is complete).
bool settingsBackupToSd();
// Reads a settings file from the card, validates it, applies and saves it.
// Returns "" on success or an error message.
String settingsRestoreFromSd(const String &path);
// Upper-cases, strips a ".BK" suffix and checks the characters SET symbols use.
bool normaliseSymbol(String &sym);
