#include "market.h"

#include "storage.h"

// Official SET 2026 holidays (weekday closures), from SET's "Calendar 2026".
static const char BUILTIN_HOLIDAYS[] =
    "# SET market holidays, one per line: YYYY-MM-DD  Name\n"
    "# Lines starting with # are ignored. Edit this file or use the settings page.\n"
    "2026-01-01  New Year's Day\n"
    "2026-01-02  Additional special holiday\n"
    "2026-03-03  Makha Bucha Day\n"
    "2026-04-06  Chakri Memorial Day\n"
    "2026-04-13  Songkran Festival\n"
    "2026-04-14  Songkran Festival\n"
    "2026-04-15  Songkran Festival\n"
    "2026-05-01  National Labour Day\n"
    "2026-05-04  Coronation Day\n"
    "2026-06-01  Substitution for Visakha Bucha Day\n"
    "2026-06-03  H.M. Queen Suthida's Birthday\n"
    "2026-07-28  H.M. King Maha Vajiralongkorn's Birthday\n"
    "2026-07-29  Asarnha Bucha Day\n"
    "2026-08-12  H.M. Queen Sirikit The Queen Mother's Birthday\n"
    "2026-10-13  H.M. King Bhumibol Adulyadej The Great Memorial Day\n"
    "2026-10-23  H.M. King Chulalongkorn The Great Memorial Day\n"
    "2026-12-07  Substitution for H.M. King Bhumibol's Birthday\n"
    "2026-12-10  Constitution Day\n"
    "2026-12-31  New Year's Eve\n";

constexpr int MAX_HOLIDAYS = 120;

struct Holiday {
  uint32_t ymd;  // e.g. 20261013
  String name;
};

static Holiday holidays[MAX_HOLIDAYS];
static int nHolidays = 0;
static String activeText;
static String source = "built-in list";

// Parses the text; returns "" on success or an error naming the bad line.
static String parse(const String &text, Holiday *out, int &count) {
  count = 0;
  int lineNo = 0, start = 0;
  while (start <= (int)text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = text.substring(start, end);
    start = end + 1;
    lineNo++;
    line.trim();
    if (line.isEmpty() || line.startsWith("#")) continue;
    int y, m, d;
    if (line.length() < 10 || sscanf(line.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3 || line[4] != '-' ||
        line[7] != '-' || y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31)
      return "Line " + String(lineNo) + ": expected YYYY-MM-DD, got \"" + line + "\"";
    if (count >= MAX_HOLIDAYS) return "Too many holidays (max " + String(MAX_HOLIDAYS) + ")";
    out[count].ymd = y * 10000 + m * 100 + d;
    String name = line.substring(10);
    name.trim();
    out[count].name = name.isEmpty() ? String("Holiday") : name;
    count++;
  }
  return "";
}

static void activate(const String &text, const String &from) {
  int n;
  if (parse(text, holidays, n).isEmpty()) {
    nHolidays = n;
    activeText = text;
    source = from;
  }
}

void holidaysLoad() {
  activate(BUILTIN_HOLIDAYS, "built-in list (no SD card)");
  if (!sdReady()) return;
  String text;
  if (sdReadFile(SD_HOLIDAYS_FILE, text)) {
    static Holiday tmp[MAX_HOLIDAYS];
    int n;
    String err = parse(text, tmp, n);
    if (err.isEmpty()) activate(text, "SD card " SD_HOLIDAYS_FILE);
    else Serial.printf("holidays.txt ignored: %s\n", err.c_str());
  } else if (sdWriteFile(SD_HOLIDAYS_FILE, BUILTIN_HOLIDAYS)) {
    source = "SD card " SD_HOLIDAYS_FILE;
  }
  Serial.printf("Holidays: %d (%s)\n", nHolidays, source.c_str());
}

String holidaysText() { return activeText; }
String holidaysSource() { return source; }

String holidaysSave(const String &text) {
  static Holiday tmp[MAX_HOLIDAYS];
  int n;
  String err = parse(text, tmp, n);
  if (!err.isEmpty()) return err;
  if (!sdReady()) return "No SD card: insert a card to save the holiday list";
  if (!sdWriteFile(SD_HOLIDAYS_FILE, text)) return "Could not write to the SD card";
  activate(text, "SD card " SD_HOLIDAYS_FILE);
  return "";
}

bool isHoliday(time_t when, String *name) {
  struct tm tm;
  localtime_r(&when, &tm);
  uint32_t ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
  for (int i = 0; i < nHolidays; i++) {
    if (holidays[i].ymd == ymd) {
      if (name) *name = holidays[i].name;
      return true;
    }
  }
  return false;
}

bool marketOpen(time_t now) {
  struct tm tm;
  localtime_r(&now, &tm);
  if (tm.tm_wday == 0 || tm.tm_wday == 6) return false;
  if (isHoliday(now)) return false;
  int m = tm.tm_hour * 60 + tm.tm_min;
  return (m >= 600 && m < 750) || (m >= 870 && m < 1000);
}
