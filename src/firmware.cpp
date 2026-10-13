#include "firmware.h"

#include <SD_MMC.h>
#include <Update.h>

#include "storage.h"

bool firmwarePendingOnSd() { return sdExists(SD_FIRMWARE_FILE); }

String firmwareInstallFromSd(void (*progress)(int percent)) {
  File f = SD_MMC.open(SD_FIRMWARE_FILE, FILE_READ);
  if (!f) return "Cannot open " SD_FIRMWARE_FILE;
  size_t size = f.size();
  String err;
  if (size < 64 * 1024 || f.peek() != 0xE9) {  // ESP image magic byte
    err = "firmware.bin is not an ESP32 firmware image";
  } else if (!Update.begin(size)) {
    err = String("Not enough space for update: ") + Update.errorString();
  } else {
    static uint8_t chunk[4096];
    size_t done = 0;
    int lastPct = -1;
    while (done < size) {
      int n = f.read(chunk, sizeof(chunk));
      if (n <= 0 || Update.write(chunk, n) != (size_t)n) break;
      done += n;
      int pct = done * 100 / size;
      if (progress && pct / 10 != lastPct / 10) progress(pct);  // every 10%
      lastPct = pct;
    }
    if (done != size) {
      err = String("Write failed: ") + Update.errorString();
      Update.abort();
    } else if (!Update.end(true)) {
      err = String("Verify failed: ") + Update.errorString();
    }
  }
  f.close();
  // Never retry the same file: keep it for reference under a new name.
  sdRename(SD_FIRMWARE_FILE, err.isEmpty() ? SD_DIR "/firmware-installed.bin" : SD_DIR "/firmware-failed.bin");
  return err;
}
