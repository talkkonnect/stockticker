#pragma once
// Yahoo Finance quote + intraday series for SET symbols (Yahoo suffix ".BK").

#include <Arduino.h>
#include "settings.h"

constexpr int MAX_POINTS = 420;  // one trading day of 1-minute bars

struct Quote {
  bool valid = false;
  String error;            // last fetch error, "" if the last fetch succeeded
  time_t fetchedAt = 0;    // when the data was last refreshed successfully
  String name;
  float price = 0, prevClose = 0, dayHigh = 0, dayLow = 0;
  float high52 = 0, low52 = 0;
  int64_t volume = 0;
  time_t marketTime = 0;
  time_t sessionStart = 0, sessionEnd = 0;
  int n = 0;
  time_t ts[MAX_POINTS];
  float close[MAX_POINTS];
};

extern Quote quotes[MAX_STOCKS];

// SET index (Yahoo "^SET.BK"), shown alongside the focused stock.
struct IndexQuote {
  bool valid = false;
  float price = 0, prevClose = 0;
  time_t fetchedAt = 0;
  String error;
};
extern IndexQuote setIndex;
bool fetchSetIndex();

// Fetches the symbol into q; on failure keeps the old data and sets q.error.
bool fetchQuote(const String &symbol, Quote &q);

// Lightweight check used by the portal: is this a SET-listed symbol?
// Returns "" on success and fills name/price, otherwise an error message.
String lookupSymbol(const String &symbol, String &name, float &price);
