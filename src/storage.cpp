#include "storage.h"

#include <FS.h>
#include <SD_MMC.h>

// Board wiring (ESP32-S3-ePaper-3.97 micro-SD slot).
constexpr int SD_CLK = 16, SD_CMD = 17, SD_D0 = 15, SD_D1 = 7, SD_D2 = 8, SD_D3 = 18;

static bool ready = false;

bool sdBegin() {
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  ready = SD_MMC.begin("/sdcard", false);  // 4-bit bus
  if (!ready) {
    SD_MMC.end();
    ready = SD_MMC.begin("/sdcard", true);  // some cards only work in 1-bit mode
  }
  if (!ready || SD_MMC.cardType() == CARD_NONE) {
    ready = false;
    Serial.println("SD card: not found");
    return false;
  }
  if (!SD_MMC.exists(SD_DIR)) SD_MMC.mkdir(SD_DIR);
  if (!SD_MMC.exists(SD_SOUNDS_DIR)) SD_MMC.mkdir(SD_SOUNDS_DIR);
  Serial.printf("SD card: %llu MB, %llu MB used\n", SD_MMC.totalBytes() >> 20, SD_MMC.usedBytes() >> 20);
  return true;
}

bool sdReady() { return ready; }
uint64_t sdTotalBytes() { return ready ? SD_MMC.totalBytes() : 0; }
uint64_t sdUsedBytes() { return ready ? SD_MMC.usedBytes() : 0; }

bool sdExists(const String &path) { return ready && SD_MMC.exists(path); }

size_t sdFileSize(const String &path) {
  if (!ready) return 0;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return 0;
  size_t n = f.size();
  f.close();
  return n;
}

bool sdReadFile(const String &path, String &out, size_t maxBytes) {
  if (!ready) return false;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f || f.isDirectory()) return false;
  if (f.size() > maxBytes) {
    f.close();
    return false;
  }
  out = f.readString();
  f.close();
  return true;
}

bool sdWriteFile(const String &path, const String &data) {
  if (!ready) return false;
  String tmp = path + ".tmp";
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return false;
  bool ok = f.print(data) == data.length();
  f.close();
  if (!ok) {
    SD_MMC.remove(tmp);
    return false;
  }
  return sdRename(tmp, path);
}

bool sdRename(const String &from, const String &to) {
  if (!ready) return false;
  if (SD_MMC.exists(to)) SD_MMC.remove(to);
  return SD_MMC.rename(from, to);
}

int sdListSounds(String names[], int max) {
  if (!ready) return 0;
  File dir = SD_MMC.open(SD_SOUNDS_DIR);
  if (!dir || !dir.isDirectory()) return 0;
  int n = 0;
  for (File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
    String name = f.name();
    String lower = name;
    lower.toLowerCase();
    if (!f.isDirectory() && lower.endsWith(".wav") && !name.startsWith(".")) names[n++] = name;
    f.close();
  }
  dir.close();
  return n;
}

String findAlarmSound(const String &symbol, bool isShort) {
  if (!ready) return "";
  const char *side = isShort ? "short" : "long";
  String candidates[] = {
      String(SD_SOUNDS_DIR "/") + symbol + "-" + side + ".wav",
      String(SD_SOUNDS_DIR "/") + symbol + ".wav",
      String(SD_SOUNDS_DIR "/") + side + ".wav",
      String(SD_SOUNDS_DIR "/alarm.wav"),
  };
  for (const String &p : candidates)
    if (SD_MMC.exists(p)) return p;
  return "";
}
