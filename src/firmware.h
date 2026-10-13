#pragma once
// Firmware update from the SD card: copy a build's firmware.bin to
// /ticker/firmware.bin and it is installed at the next power-on. Afterwards
// the file is renamed (firmware-installed.bin / firmware-failed.bin) so it is
// never installed twice.

#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

bool firmwarePendingOnSd();

// Installs the pending image, reporting 0-100 progress. Returns "" on success
// (caller should restart) or an error message.
String firmwareInstallFromSd(void (*progress)(int percent));
