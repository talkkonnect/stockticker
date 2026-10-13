#pragma once
// Settings web portal. Runs on the board's LAN IP in normal mode, and as a
// captive portal on the board's own hotspot in setup (AP) mode.

#include <Arduino.h>

void portalBegin(bool apMode, const String &apSsid);
void portalLoop();

// Provided by main: restart into setup (hotspot) mode.
void restartIntoSetupMode();
