# SET e-paper stock ticker

Waveshare ESP32-S3-ePaper-3.97 (800x480) ticker for up to 3 Stock Exchange of
Thailand symbols, with long/short position P/L and a target-price alarm.
Prices come from Yahoo Finance (`<SYMBOL>.BK`).

## Screen
- Header: symbol, company name, page dots, ALARM badge, market open/closed.
- Big price and change, day/52-week stats, intraday chart with target lines.
- Positions table: shares, entry, target, distance to target, P/L now (THB, %),
  P/L at target. Rows whose target is reached are shown inverted.
- Strip with every stock: price, change, total P/L and the time it was updated.
- Footer: last update time, portfolio P/L, settings URL.

## Settings portal
Open `http://<ticker IP>` (shown in the footer) on the same network, or use
setup mode:
- Hold **MENU** for 3 seconds (or hold MENU/BOOT while powering on), or press
  "Start setup hotspot" in the portal.
- Join the `StockTicker-XXXX` Wi-Fi (default password `stockticker`, or scan
  the QR code on screen) and open `http://192.168.4.1`.
- Setup mode starts automatically when no network is configured or none of the
  saved networks can be joined (it retries the networks after 10 minutes).

Settings: up to 4 Wi-Fi networks, up to 3 SET symbols with up to 3 positions
each (long: bought at / sell at; short: sold at / buy back at; shares), alarm
on/off, volume and duration, update and page-rotation intervals, hotspot password.

## SD card
Everything lives in `/ticker` on the card (FAT32). Files can be copied with a
computer or uploaded from the settings page.

| File | Purpose |
|---|---|
| `settings.json` | Settings backup, rewritten on every save (contains Wi-Fi passwords). "Restore from card" in the portal applies it. |
| `restore.json` | Settings file to apply at the next power-on; renamed to `restore-applied.json` afterwards. |
| `sounds/*.wav` | Alarm sounds, PCM 8/16-bit, mono/stereo, 8-48 kHz. First match wins: `SJWD-long.wav` / `SJWD-short.wav`, `SJWD.wav`, `long.wav` / `short.wav`, `alarm.wav`; otherwise the built-in beep. |
| `holidays.txt` | SET holidays, `YYYY-MM-DD  Name` per line; the official 2026 list is written on first use. Without a card the built-in 2026 list is used. |
| `firmware.bin` | Firmware installed at the next power-on (or "Install now" in the portal); renamed to `firmware-installed.bin` / `firmware-failed.bin` afterwards. Use `.pio/build/esp32s3-epaper-397/firmware.bin`. |

The firmware version is `FW_VERSION` in `platformio.ini`; bump it for each release
so the portal shows which build is running.

## Buttons
UP / DOWN switch stock page; MENU silences the alarm (any button does); MENU
held 3 s enters setup mode; MENU in setup mode leaves without changes.

## Build and flash
```
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/ttyACM0
```
`http://<ticker IP>/screenshot.pbm` returns the current screen image.
`pio run -e diag -t upload` flashes a display/Wi-Fi hardware diagnostic.
