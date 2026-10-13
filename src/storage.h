#pragma once
// SD card (4-bit SD_MMC). Everything the ticker uses lives under /ticker:
//   /ticker/settings.json   automatic settings backup (written on every save)
//   /ticker/restore.json    put a settings file here; it is applied at power-on
//   /ticker/holidays.txt    SET market holidays, one "YYYY-MM-DD  Name" per line
//   /ticker/sounds/*.wav    custom alarm sounds (see findAlarmSound)
//   /ticker/firmware.bin    put a firmware image here; it is installed at power-on

#include <Arduino.h>

#define SD_DIR "/ticker"
#define SD_SETTINGS_BACKUP SD_DIR "/settings.json"
#define SD_RESTORE_FILE SD_DIR "/restore.json"
#define SD_HOLIDAYS_FILE SD_DIR "/holidays.txt"
#define SD_SOUNDS_DIR SD_DIR "/sounds"
#define SD_FIRMWARE_FILE SD_DIR "/firmware.bin"

bool sdBegin();
bool sdReady();
uint64_t sdTotalBytes();
uint64_t sdUsedBytes();

bool sdExists(const String &path);
size_t sdFileSize(const String &path);
bool sdReadFile(const String &path, String &out, size_t maxBytes = 64 * 1024);
bool sdWriteFile(const String &path, const String &data);  // atomic: temp file + rename
bool sdRename(const String &from, const String &to);       // replaces 'to'

// WAV files in /ticker/sounds (names only).
int sdListSounds(String names[], int max);

// Custom alarm sound for a position, most specific first:
//   <SYMBOL>-long.wav / <SYMBOL>-short.wav, <SYMBOL>.wav, long.wav / short.wav, alarm.wav
// Returns the full path, or "" to use the built-in beep.
String findAlarmSound(const String &symbol, bool isShort);
