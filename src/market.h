#pragma once
// SET trading calendar: trading hours plus a holiday list kept on the SD card
// (/ticker/holidays.txt). Without a card the built-in list is used.

#include <Arduino.h>
#include <time.h>

// Loads holidays from the card; writes the built-in list there if the card has none.
void holidaysLoad();

// The current list as editable text ("YYYY-MM-DD  Name" per line).
String holidaysText();

// Validates, stores on the card and activates a new list. Returns "" or an error.
String holidaysSave(const String &text);

// Where the active list came from, for the portal.
String holidaysSource();

// If 'when' falls on a listed holiday, returns true and sets name.
bool isHoliday(time_t when, String *name = nullptr);

// Continuous trading 10:00-12:30 and 14:30-16:40, Mon-Fri, excluding holidays.
bool marketOpen(time_t now);
