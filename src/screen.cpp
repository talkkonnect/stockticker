#include "screen.h"

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <qrcode.h>
#include <time.h>

#include "epd397.h"
#include "settings.h"
#include "quotes.h"
#include "portfolio.h"
#include "market.h"
#include "firmware.h"

// Board wiring (fixed on the ESP32-S3-ePaper-3.97).
constexpr int EPD_BUSY = 3, EPD_DC = 9, EPD_CS = 10, EPD_SCLK = 11, EPD_MOSI = 12, EPD_RST = 46;

constexpr int FULL_REFRESH_EVERY = 30;  // partial refreshes between full (de-ghosting) refreshes

// Canvas bit set = white, matching the panel's RAM format.
constexpr uint16_t BLACK = 0;
constexpr uint16_t WHITE = 1;
constexpr int W = Epd397::WIDTH;
constexpr int H = Epd397::HEIGHT;

static Epd397 epd(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY, EPD_SCLK, EPD_MOSI);
static GFXcanvas1 display(W, H);
static U8G2_FOR_ADAFRUIT_GFX u8g2;
static uint32_t pushCount = 0;

// ---------------------------------------------------------------- helpers

static String formatThousands(int64_t v) {
  bool neg = v < 0;
  String s = String((unsigned long long)(neg ? -v : v));
  for (int i = s.length() - 3; i > 0; i -= 3) s = s.substring(0, i) + "," + s.substring(i);
  return neg ? "-" + s : s;
}

// 2-decimal money with thousands separators and an explicit sign.
static String formatMoney(double v, bool sign = true) {
  int64_t cents = llround(fabs(v) * 100);
  String s = formatThousands(cents / 100) + "." + (cents % 100 < 10 ? "0" : "") + String((int)(cents % 100));
  if (cents == 0) return s;
  if (v < 0) return "-" + s;
  return sign ? "+" + s : s;
}

static String formatPct(double v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%+.2f%%", v);
  return buf;
}

static String formatTime(time_t t, const char *fmt) {
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[40];
  strftime(buf, sizeof(buf), fmt, &tm);
  return buf;
}

static int textWidth(const String &s, const uint8_t *font) {
  u8g2.setFont(font);
  return u8g2.getUTF8Width(s.c_str());
}

// align: 0 = left, 1 = centre, 2 = right
static void textAt(int x, int y, const String &s, const uint8_t *font, int align = 0) {
  u8g2.setFont(font);
  int w = u8g2.getUTF8Width(s.c_str());
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  u8g2.drawUTF8(x, y, s.c_str());
}

static void inkWhite() {
  u8g2.setForegroundColor(WHITE);
  u8g2.setBackgroundColor(BLACK);
}

static void inkBlack() {
  u8g2.setForegroundColor(BLACK);
  u8g2.setBackgroundColor(WHITE);
}

static void dashedHLine(int x0, int x1, int y, int on, int off) {
  for (int x = x0; x < x1; x += on + off) display.drawFastHLine(x, y, min(on, x1 - x), BLACK);
}

// ---------------------------------------------------------------- ticker page parts

static void drawHeader(int focus, bool alarmActive, time_t now) {
  const StockCfg &sc = settings.stocks[focus];
  const Quote &q = quotes[focus];
  display.fillRect(0, 0, W, 48, BLACK);
  inkWhite();
  textAt(16, 35, sc.symbol, u8g2_font_helvB24_tf);
  int nameX = 16 + textWidth(sc.symbol, u8g2_font_helvB24_tf) + 16;

  int right = W - 16;
  String market = marketOpen(now) ? "MARKET OPEN" : isHoliday(now) ? "HOLIDAY: CLOSED" : "MARKET CLOSED";
  textAt(right, 32, market, u8g2_font_helvB14_tf, 2);
  right -= textWidth(market, u8g2_font_helvB14_tf) + 20;

  // Page dots, one per configured stock; the focused one filled.
  if (settings.nStocks > 1) {
    for (int i = settings.nStocks - 1; i >= 0; i--) {
      int cx = right - 6;
      if (i == focus) display.fillCircle(cx, 26, 6, WHITE);
      else display.drawCircle(cx, 26, 6, WHITE);
      right -= 18;
    }
    right -= 10;
  }

  if (alarmActive) {
    const String a = "ALARM";
    int w = textWidth(a, u8g2_font_helvB14_tf) + 16;
    display.fillRoundRect(right - w, 10, w, 28, 4, WHITE);
    inkBlack();
    textAt(right - w / 2, 31, a, u8g2_font_helvB14_tf, 1);
    inkWhite();
    right -= w + 12;
  }

  // Company name, clipped to the space left.
  String name = q.valid ? q.name : String("SET Thailand");
  while (name.length() > 3 && nameX + textWidth(name, u8g2_font_helvR14_tf) > right - 8)
    name = name.substring(0, name.length() - 4) + "...";
  textAt(nameX, 33, name, u8g2_font_helvR14_tf);
  inkBlack();
}

static void drawStat(int x, int y, const char *label, const String &value) {
  textAt(x, y, label, u8g2_font_helvR12_tf);
  textAt(x, y + 22, value, u8g2_font_helvB18_tf);
}

// SET index centred in the gap between the big price and the stats.
// Skipped if a very wide price leaves no room.
static void drawSetIndex(int priceRight) {
  if (!setIndex.valid) return;
  const int gapRight = 505;
  String value = formatMoney(setIndex.price, false);
  float chg = setIndex.price - setIndex.prevClose;
  float pct = setIndex.prevClose ? chg / setIndex.prevClose * 100 : 0;
  char buf[40];
  snprintf(buf, sizeof(buf), "%+.2f (%+.2f%%)", chg, pct);
  String change = buf;
  int wValue = textWidth(value, u8g2_font_helvB24_tf);
  int wChange = textWidth(change, u8g2_font_helvB12_tf) + 16;  // + arrow
  int wBlock = max(wValue, wChange);
  int cx = max(410, priceRight + 14 + wBlock / 2);  // centre, nudged right if the price is wide
  if (cx + wBlock / 2 > gapRight) return;

  textAt(cx, 72, "SET INDEX", u8g2_font_helvB12_tf, 1);
  textAt(cx, 104, value, u8g2_font_helvB24_tf, 1);
  int ax = cx - wChange / 2, ay = 127;  // small up/down arrow before the change
  textAt(ax + 16, ay, change, u8g2_font_helvB12_tf);
  if (chg > 0.0001f) display.fillTriangle(ax, ay, ax + 11, ay, ax + 5, ay - 10, BLACK);
  else if (chg < -0.0001f) display.fillTriangle(ax, ay - 10, ax + 11, ay - 10, ax + 5, ay, BLACK);
  else display.fillRect(ax, ay - 6, 11, 3, BLACK);
}

static void drawPriceBlock(const Quote &q) {
  // Big price
  String price = String(q.price, 2);
  u8g2.setFont(u8g2_font_logisoso78_tn);
  int pw = u8g2.getUTF8Width(price.c_str());
  u8g2.drawUTF8(16, 136, price.c_str());
  textAt(26 + pw, 136, "THB", u8g2_font_helvB18_tf);
  drawSetIndex(26 + pw + textWidth("THB", u8g2_font_helvB18_tf));

  // Change vs previous close
  float chg = q.price - q.prevClose;
  float pct = q.prevClose ? chg / q.prevClose * 100.0f : 0;
  int ax = 16, ay = 180;
  if (chg > 0.0001f) display.fillTriangle(ax, ay, ax + 24, ay, ax + 12, ay - 20, BLACK);
  else if (chg < -0.0001f) display.fillTriangle(ax, ay - 20, ax + 24, ay - 20, ax + 12, ay, BLACK);
  else display.fillRect(ax, ay - 12, 24, 5, BLACK);
  char buf[48];
  snprintf(buf, sizeof(buf), "%+.2f  (%+.2f%%)", chg, pct);
  textAt(ax + 36, ay, buf, u8g2_font_helvB24_tf);

  // Stats on the right
  const int sx = 520, sx2 = 660;
  drawStat(sx, 68, "Prev close", String(q.prevClose, 2));
  drawStat(sx2, 68, "Volume", formatThousands(q.volume));
  drawStat(sx, 110, "Day high", String(q.dayHigh, 2));
  drawStat(sx2, 110, "Day low", String(q.dayLow, 2));
  drawStat(sx, 152, "52W high", String(q.high52, 2));
  drawStat(sx2, 152, "52W low", String(q.low52, 2));
}

// Calendar days from an entry date ("YYYY-MM-DD") to today; -1 if unknown/future.
static int daysHeld(const String &date, time_t now) {
  int y, m, d;
  if (date.length() != 10 || sscanf(date.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return -1;
  if (now < 1700000000) return -1;  // clock not set yet
  struct tm t;
  localtime_r(&now, &t);
  // Day numbers via UTC-style arithmetic on calendar dates (no DST in Thailand anyway).
  auto dayNum = [](int y, int m, int d) {
    struct tm x = {};
    x.tm_year = y - 1900;
    x.tm_mon = m - 1;
    x.tm_mday = d;
    x.tm_hour = 12;
    return (long)(mktime(&x) / 86400);
  };
  long days = dayNum(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) - dayNum(y, m, d);
  return days < 0 ? -1 : (int)days;
}

// Black "TARGET REACHED" badge beside the change line, naming the position(s).
static void drawTargetBadge(const StockCfg &sc, const bool hit[3]) {
  int n = 0, first = -1;
  for (int i = 0; i < sc.nPos; i++)
    if (hit[i]) {
      if (first < 0) first = i;
      n++;
    }
  if (n == 0) return;
  const String title = "TARGET REACHED";
  const Position &p = sc.pos[first];
  String detail = n > 1 ? String(n) + " positions"
                        : String(p.isShort ? "Short " : "Long ") + (first + 1) + " @ " + String(p.target, 2);
  // Sized to its text, right-aligned against the stats column.
  const int w = max(textWidth(title, u8g2_font_helvB14_tf), textWidth(detail, u8g2_font_helvB12_tf)) + 24;
  const int h = 46, x = 510 - w, y = 146;
  display.fillRoundRect(x, y, w, h, 6, BLACK);
  inkWhite();
  textAt(x + w / 2, y + 20, title, u8g2_font_helvB14_tf, 1);
  textAt(x + w / 2, y + 39, detail, u8g2_font_helvB12_tf, 1);
  inkBlack();
}

// Positions table. Returns the y just below it.
static int drawPositions(int y0, const StockCfg &sc, const Quote &q, const bool hit[3]) {
  // Column right edges
  const int cShares = 200, cEntry = 285, cTarget = 365, cToTgt = 472, cPL = 590, cPct = 672, cAtTgt = W - 16;
  const uint8_t *hdr = u8g2_font_helvB10_tf;
  // Bold throughout: thin 1-px strokes look grey on e-paper.
  const uint8_t *row = u8g2_font_helvB14_tf;
  const uint8_t *rowB = u8g2_font_helvB14_tf;
  constexpr int ROW_H = 24;

  display.drawFastHLine(16, y0, W - 32, BLACK);
  int y = y0 + 16;
  textAt(16, y, "POSITION", hdr);
  textAt(cShares, y, "SHARES", hdr, 2);
  textAt(cEntry, y, "ENTRY", hdr, 2);
  textAt(cTarget, y, "TARGET", hdr, 2);
  textAt(cToTgt, y, "TO TARGET", hdr, 2);
  textAt(cPL, y, "P/L THB", hdr, 2);
  textAt(cPct, y, "P/L %", hdr, 2);
  textAt(cAtTgt, y, "AT TARGET", hdr, 2);
  y += 6;

  double totalPL = 0, totalCost = 0, totalAtTgt = 0;
  for (int i = 0; i < sc.nPos; i++) {
    const Position &p = sc.pos[i];
    int base = y + 20;
    if (hit[i]) {
      display.fillRect(12, y + 2, W - 24, ROW_H, BLACK);
      inkWhite();
    }
    // "Long 1 (3)": days waited since the entry date, when one is set.
    String label = String(p.isShort ? "Short " : "Long ") + (i + 1);
    textAt(16, base, label, rowB);
    int held = daysHeld(p.date, time(nullptr));
    if (held >= 0) textAt(16 + textWidth(label, rowB) + 5, base, "(" + String(held) + ")", u8g2_font_helvB12_tf);
    textAt(cShares, base, formatThousands(p.volume), row, 2);
    textAt(cEntry, base, String(p.entry, 2), row, 2);
    textAt(cTarget, base, p.target > 0 ? String(p.target, 2) : String("-"), row, 2);
    if (hit[i]) textAt(cToTgt, base, "REACHED", rowB, 2);
    else if (p.target > 0 && q.valid && q.price > 0) textAt(cToTgt, base, formatPct((p.target - q.price) / q.price * 100), row, 2);
    else textAt(cToTgt, base, "-", row, 2);
    if (q.valid) {
      double pl = positionPL(p, q.price);
      textAt(cPL, base, formatMoney(pl), rowB, 2);
      textAt(cPct, base, formatPct(positionPLPercent(p, q.price)), row, 2);
      totalPL += pl;
    }
    textAt(cAtTgt, base, p.target > 0 ? formatMoney(positionPLAtTarget(p)) : String("-"), row, 2);
    totalCost += (double)toSatang(p.entry) * p.volume / 100.0;
    totalAtTgt += positionPLAtTarget(p);
    inkBlack();
    y += ROW_H;
  }

  if (sc.nPos > 1) {
    display.drawFastHLine(cShares - 60, y + 4, W - 16 - (cShares - 60), BLACK);
    int base = y + 24;
    textAt(16, base, "Total", rowB);
    if (q.valid) {
      textAt(cPL, base, formatMoney(totalPL), rowB, 2);
      textAt(cPct, base, formatPct(totalCost > 0 ? totalPL / totalCost * 100 : 0), row, 2);
    }
    textAt(cAtTgt, base, formatMoney(totalAtTgt), row, 2);
    y += 28;
  }
  return y + 6;
}

static void drawChart(int x0, int y0, int w, int h, const StockCfg &sc, const Quote &q, const bool hit[3]) {
  display.drawRect(x0, y0, w, h, BLACK);
  if (q.n < 2) {
    textAt(x0 + w / 2, y0 + h / 2 + 6, "No intraday data yet", u8g2_font_helvR14_tf, 1);
    return;
  }

  float lo = q.prevClose, hi = q.prevClose;
  for (int i = 0; i < q.n; i++) {
    lo = min(lo, q.close[i]);
    hi = max(hi, q.close[i]);
  }
  if (hi - lo < 0.01f) { hi += 0.05f; lo -= 0.05f; }
  float pad = (hi - lo) * 0.1f;
  hi += pad;
  lo -= pad;

  time_t t0 = q.sessionStart ? q.sessionStart : q.ts[0];
  time_t t1 = q.sessionEnd > t0 ? q.sessionEnd : q.ts[q.n - 1];
  if (t1 <= t0) t1 = t0 + 1;

  const int left = x0 + 60, right = x0 + w - 8, top = y0 + 8, bottom = y0 + h - 20;
  auto px = [&](time_t t) { return left + (int)((long)(t - t0) * (right - left) / (long)(t1 - t0)); };
  auto py = [&](float v) { return bottom - (int)((v - lo) / (hi - lo) * (bottom - top)); };
  const uint8_t *small = u8g2_font_helvR10_tf;

  textAt(left - 6, top + 10, String(hi, 2), small, 2);
  textAt(left - 6, bottom, String(lo, 2), small, 2);

  // Previous close, dashed
  int yc = py(q.prevClose);
  dashedHLine(left, right, yc, 4, 4);
  if (abs(yc - (top + 5)) > 12 && abs(yc - (bottom - 5)) > 12)  // skip if it would overprint hi/lo
    textAt(left - 6, yc + 4, String(q.prevClose, 2), small, 2);

  // Target prices that fall inside the chart's range, dotted and labelled
  for (int i = 0; i < sc.nPos; i++) {
    const Position &p = sc.pos[i];
    if (p.target <= 0 || p.target < lo || p.target > hi) continue;
    int yt = py(p.target);
    if (hit[i]) display.fillRect(left, yt - 1, right - left, 3, BLACK);  // reached: solid, thick
    else dashedHLine(left, right, yt, 1, 3);
    textAt(right - 4, yt - 3, String(p.isShort ? "S" : "L") + (i + 1) + " " + String(p.target, 2), small, 2);
  }

  textAt(left, y0 + h - 5, formatTime(t0, "%H:%M"), small, 0);
  textAt(right, y0 + h - 5, formatTime(t1, "%H:%M"), small, 2);

  // Price line (2px thick); bridge the lunch break with a straight segment
  for (int i = 1; i < q.n; i++) {
    int xa = px(q.ts[i - 1]), ya = py(q.close[i - 1]);
    int xb = px(q.ts[i]), yb = py(q.close[i]);
    display.drawLine(xa, ya, xb, yb, BLACK);
    display.drawLine(xa, ya + 1, xb, yb + 1, BLACK);
  }
  display.fillCircle(px(q.ts[q.n - 1]), py(q.close[q.n - 1]), 4, BLACK);
}

// One cell per stock: symbol, price, change, total P/L and when it was fetched.
static void drawStrip(int y0, int focus, const bool hit[][3]) {
  const int cellW = (W - 32) / MAX_STOCKS;
  display.drawFastHLine(16, y0, W - 32, BLACK);
  for (int s = 0; s < settings.nStocks; s++) {
    const StockCfg &sc = settings.stocks[s];
    const Quote &q = quotes[s];
    int x = 16 + s * cellW, xr = x + cellW - 10;
    if (s == focus) {
      display.fillRect(x, y0 + 3, cellW - 4, 36, BLACK);
      inkWhite();
    } else if (s > 0) {
      display.drawFastVLine(x - 2, y0 + 6, 30, BLACK);
    }
    textAt(x + 6, y0 + 19, sc.symbol, u8g2_font_helvB12_tf);
    if (q.valid) {
      float pct = q.prevClose ? (q.price - q.prevClose) / q.prevClose * 100 : 0;
      textAt(xr, y0 + 19, String(q.price, 2) + "  " + formatPct(pct), u8g2_font_helvB12_tf, 2);
    }
    String line2;
    if (!q.error.isEmpty()) line2 = "! " + q.error;
    else if (sc.nPos > 0 && q.valid) {
      double pl = 0;
      for (int i = 0; i < sc.nPos; i++) pl += positionPL(sc.pos[i], q.price);
      line2 = "P/L " + formatMoney(pl);
    } else line2 = "No positions";
    textAt(x + 6, y0 + 35, line2, u8g2_font_helvB10_tf);
    bool anyHit = false;
    for (int i = 0; i < sc.nPos; i++) anyHit |= hit[s][i];
    if (anyHit) {  // tag in the opposite colour so it stands out on either background
      const String tag = "TARGET";
      int tw = textWidth(tag, u8g2_font_helvB10_tf) + 8;
      display.fillRect(xr - tw, y0 + 24, tw, 14, s == focus ? WHITE : BLACK);
      if (s == focus) inkBlack();
      else inkWhite();
      textAt(xr - tw / 2, y0 + 35, tag, u8g2_font_helvB10_tf, 1);
      if (s == focus) inkWhite();
      else inkBlack();
    } else if (q.fetchedAt) {
      textAt(xr, y0 + 35, formatTime(q.fetchedAt, "%H:%M:%S"), u8g2_font_helvB10_tf, 2);
    }
    inkBlack();
  }
}

static void drawFooter(const String &ip, time_t lastUpdate) {
  display.drawFastHLine(0, 456, W, BLACK);
  String left = "Updated " + (lastUpdate ? formatTime(lastUpdate, "%a %d %b  %H:%M:%S") : String("--"));
  textAt(16, 473, left, u8g2_font_helvR12_tf);

  bool anyPos = false, allValid = true;
  double total = 0;
  for (int s = 0; s < settings.nStocks; s++) {
    for (int i = 0; i < settings.stocks[s].nPos; i++) {
      anyPos = true;
      if (quotes[s].valid) total += positionPL(settings.stocks[s].pos[i], quotes[s].price);
      else allValid = false;
    }
  }
  if (anyPos) textAt(W / 2 + 10, 473, "Portfolio P/L " + formatMoney(total) + " THB" + (allValid ? "" : "*"), u8g2_font_helvB12_tf, 1);
  if (!ip.isEmpty()) textAt(W - 16, 473, "Settings: http://" + ip, u8g2_font_helvR12_tf, 2);
}

// ---------------------------------------------------------------- public

void screenBegin() {
  epd.begin();
  u8g2.begin(display);
  u8g2.setFontMode(1);
  inkBlack();
}

void drawTicker(int focus, const bool hit[][3], bool alarmActive, const String &ip, time_t lastUpdate) {
  time_t now = time(nullptr);
  const StockCfg &sc = settings.stocks[focus];
  const Quote &q = quotes[focus];

  display.fillScreen(WHITE);
  drawHeader(focus, alarmActive, now);

  const int chartBottom = 410;
  if (!q.valid) {
    textAt(W / 2, 140, "Waiting for data...", u8g2_font_helvB24_tf, 1);
    if (!q.error.isEmpty()) textAt(W / 2, 175, q.error, u8g2_font_helvR14_tf, 1);
    int y = 196;
    if (sc.nPos > 0) y = drawPositions(196, sc, q, hit[focus]);
    (void)y;
  } else {
    drawPriceBlock(q);
    int y = 194;
    if (sc.nPos > 0) y = drawPositions(194, sc, q, hit[focus]);
    drawChart(16, y + 2, W - 32, chartBottom - y - 2, sc, q, hit[focus]);
    drawTargetBadge(sc, hit[focus]);
  }
  drawStrip(414, focus, hit);
  drawFooter(ip, lastUpdate);
}

void drawSetup(const String &apSsid, const String &apPass, const String &portalIp, const String &note) {
  display.fillScreen(WHITE);
  display.fillRect(0, 0, W, 48, BLACK);
  inkWhite();
  textAt(16, 35, "SETUP MODE", u8g2_font_helvB24_tf);
  textAt(W - 16, 32, "Stock Ticker", u8g2_font_helvB14_tf, 2);
  inkBlack();

  textAt(24, 92, "1. Connect your phone or computer to Wi-Fi", u8g2_font_helvB18_tf);
  textAt(48, 128, "Network:", u8g2_font_helvR18_tf);
  textAt(170, 128, apSsid, u8g2_font_helvB18_tf);
  textAt(48, 160, "Password:", u8g2_font_helvR18_tf);
  textAt(170, 160, apPass, u8g2_font_helvB18_tf);
  textAt(24, 214, "2. Open the settings page", u8g2_font_helvB18_tf);
  textAt(48, 250, "http://" + portalIp, u8g2_font_helvB24_tf);
  textAt(48, 280, "It usually opens by itself after you join.", u8g2_font_helvR14_tf);
  textAt(24, 330, "3. Save. The ticker restarts with the new settings.", u8g2_font_helvB18_tf);
  if (!note.isEmpty()) textAt(24, 390, note, u8g2_font_helvB14_tf);
  textAt(24, 460, "Press MENU to leave setup without changes.", u8g2_font_helvR14_tf);
  textAt(W - 16, 460, "Firmware " FW_VERSION, u8g2_font_helvR12_tf, 2);

  // QR code that joins the hotspot (Wi-Fi QR format understood by phone cameras).
  static int qrX, qrY, qrScale;
  qrX = 560; qrY = 80; qrScale = 0;
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = [](esp_qrcode_handle_t qr) {
    int n = esp_qrcode_get_size(qr);
    int scale = max(1, 220 / n);
    for (int y = 0; y < n; y++)
      for (int x = 0; x < n; x++)
        if (esp_qrcode_get_module(qr, x, y)) display.fillRect(qrX + x * scale, qrY + y * scale, scale, scale, BLACK);
    qrScale = scale * n;
  };
  String wifiQr = "WIFI:T:WPA;S:" + apSsid + ";P:" + apPass + ";;";
  esp_qrcode_generate(&cfg, wifiQr.c_str());
  if (qrScale) textAt(qrX + qrScale / 2, qrY + qrScale + 24, "Scan to join", u8g2_font_helvR14_tf, 1);
}

void drawMessage(const String &title, const String &detail) {
  display.fillScreen(WHITE);
  display.fillRect(0, 0, W, 48, BLACK);
  inkWhite();
  textAt(16, 35, "Stock Ticker", u8g2_font_helvB24_tf);
  inkBlack();
  textAt(W / 2, 230, title, u8g2_font_helvB24_tf, 1);
  if (!detail.isEmpty()) textAt(W / 2, 270, detail, u8g2_font_helvR14_tf, 1);
}

void screenPush(bool forceFull) {
  uint32_t t = millis();
  bool full = forceFull || (pushCount % FULL_REFRESH_EVERY) == 0;
  if (full) epd.displayFull(display.getBuffer());
  else epd.displayPartial(display.getBuffer());
  Serial.printf("%s refresh took %lu ms\n", full ? "Full" : "Partial", millis() - t);
  pushCount++;
}

const uint8_t *screenBuffer() { return display.getBuffer(); }
