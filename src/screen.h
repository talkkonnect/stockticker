#pragma once
// Screen layouts for the 800x480 panel.

#include <Arduino.h>

void screenBegin();

// Ticker page: big view of stocks[focus] plus a strip summarising all stocks.
// hit[s][p] marks positions whose target has been reached.
void drawTicker(int focus, const bool hit[][3], bool alarmActive, const String &ip, time_t lastUpdate);

// Setup-mode page: how to join the hotspot and open the portal.
void drawSetup(const String &apSsid, const String &apPass, const String &portalIp, const String &note);

// Simple full-screen message (e.g. "Connecting to Wi-Fi...").
void drawMessage(const String &title, const String &detail);

// Sends the drawn frame to the panel. Partial refresh unless forceFull or it
// is time for the periodic full refresh that clears ghosting.
void screenPush(bool forceFull = false);

// Last frame drawn, 1 bpp (bit set = white), Epd397 layout.
const uint8_t *screenBuffer();
