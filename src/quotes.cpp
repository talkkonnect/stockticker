#include "quotes.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

Quote quotes[MAX_STOCKS];
IndexQuote setIndex;

static String yahooSymbol(const String &symbol) {
  String s = symbol;
  s.replace("&", "%26");
  s.replace("^", "%5E");
  return s + ".BK";
}

// GETs the chart endpoint and parses the parts we need. Returns "" on success.
static String getChart(const String &symbol, const char *query, JsonDocument &doc) {
  if (WiFi.status() != WL_CONNECTED) return "Wi-Fi not connected";

  WiFiClientSecure client;
  client.setInsecure();  // public market data; skip CA pinning
  HTTPClient http;
  http.useHTTP10(true);  // plain body, no chunked encoding
  http.setTimeout(15000);
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + yahooSymbol(symbol) + query;
  if (!http.begin(client, url)) return "HTTP begin failed";
  http.addHeader("User-Agent", "Mozilla/5.0 (ESP32 ePaper ticker)");
  int code = http.GET();
  if (code == 404) {
    http.end();
    return "Unknown symbol";
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    return "HTTP " + String(code);
  }
  // Body is ~22 KB and close-delimited (HTTP/1.0). Read it into one fixed
  // buffer: growing a String that size fails once the heap is fragmented.
  constexpr size_t BODY_MAX = 64 * 1024;
  static char *body = (char *)(psramFound() ? ps_malloc(BODY_MAX) : malloc(BODY_MAX));
  if (!body) {
    http.end();
    return "Out of memory";
  }
  size_t len = 0;
  WiFiClient *stream = http.getStreamPtr();
  uint32_t lastData = millis();
  while (len < BODY_MAX - 1 && millis() - lastData < 10000) {
    int avail = stream->available();
    if (avail > 0) {
      len += stream->read((uint8_t *)body + len, min((size_t)avail, BODY_MAX - 1 - len));
      lastData = millis();
    } else if (!stream->connected()) {
      break;
    } else {
      delay(5);
    }
  }
  http.end();
  if (len >= BODY_MAX - 1) return "Response too large";

  JsonDocument filter;
  JsonObject fr = filter["chart"]["result"][0].to<JsonObject>();
  fr["meta"] = true;
  fr["timestamp"] = true;
  fr["indicators"]["quote"][0]["close"] = true;
  DeserializationError err = deserializeJson(doc, body, len, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("%s: %u bytes received, JSON %s (free heap %u, largest block %u)\n", symbol.c_str(), (unsigned)len,
                  err.c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return String("JSON: ") + err.c_str();
  }

  JsonObject meta = doc["chart"]["result"][0]["meta"];
  if (meta.isNull() || meta["regularMarketPrice"].isNull()) return "Unknown symbol";
  String exch = meta["exchangeName"] | "";
  if (exch != "SET") return "Not a SET stock";
  return "";
}

bool fetchQuote(const String &symbol, Quote &q) {
  JsonDocument doc;
  String err = getChart(symbol, "?interval=1m&range=1d", doc);
  if (!err.isEmpty()) {
    q.error = err;
    return false;
  }

  JsonObject r = doc["chart"]["result"][0];
  JsonObject meta = r["meta"];
  q.name = meta["longName"] | meta["shortName"] | symbol.c_str();
  q.price = meta["regularMarketPrice"];
  q.prevClose = meta["chartPreviousClose"] | meta["previousClose"] | 0.0f;
  q.dayHigh = meta["regularMarketDayHigh"] | 0.0f;
  q.dayLow = meta["regularMarketDayLow"] | 0.0f;
  q.high52 = meta["fiftyTwoWeekHigh"] | 0.0f;
  q.low52 = meta["fiftyTwoWeekLow"] | 0.0f;
  q.volume = meta["regularMarketVolume"] | (int64_t)0;
  q.marketTime = meta["regularMarketTime"] | 0L;
  q.sessionStart = meta["currentTradingPeriod"]["regular"]["start"] | 0L;
  q.sessionEnd = meta["currentTradingPeriod"]["regular"]["end"] | 0L;

  JsonArray ts = r["timestamp"];
  JsonArray cl = r["indicators"]["quote"][0]["close"];
  q.n = 0;
  for (size_t i = 0; i < ts.size() && i < cl.size() && q.n < MAX_POINTS; i++) {
    if (cl[i].isNull()) continue;
    q.ts[q.n] = ts[i].as<long>();
    q.close[q.n] = cl[i].as<float>();
    q.n++;
  }
  q.valid = true;
  q.error = "";
  q.fetchedAt = time(nullptr);
  return true;
}

bool fetchSetIndex() {
  JsonDocument doc;
  String err = getChart("^SET", "?interval=1d&range=1d", doc);
  if (!err.isEmpty()) {
    setIndex.error = err;
    return false;
  }
  JsonObject meta = doc["chart"]["result"][0]["meta"];
  setIndex.price = meta["regularMarketPrice"] | 0.0f;
  setIndex.prevClose = meta["chartPreviousClose"] | meta["previousClose"] | 0.0f;
  setIndex.fetchedAt = time(nullptr);
  setIndex.valid = setIndex.price > 0;
  setIndex.error = "";
  return true;
}

String lookupSymbol(const String &symbol, String &name, float &price) {
  JsonDocument doc;
  String err = getChart(symbol, "?interval=1d&range=1d", doc);
  if (!err.isEmpty()) return err;
  JsonObject meta = doc["chart"]["result"][0]["meta"];
  name = meta["longName"] | meta["shortName"] | symbol.c_str();
  price = meta["regularMarketPrice"] | 0.0f;
  return "";
}
