#pragma once
// Audible alarm through the on-board ES8311 codec + NS4150B amplifier.
// Plays in its own task so fetching/drawing never blocks it.

#include <Arduino.h>

void alarmBegin();
// Starts (or restarts) ringing. wavPath: a PCM .wav on the SD card, looped for
// the duration; "" or an unplayable file falls back to the built-in beep.
void alarmRing(int seconds, int volume, const String &wavPath = "");
void alarmSilence();
bool alarmRinging();
