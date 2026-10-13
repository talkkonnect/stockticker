# SET e-paper Stock Ticker — Manual

A desk ticker for up to three Stock Exchange of Thailand (SET) stocks, running
on the **Waveshare ESP32-S3-ePaper-3.97** board (800×480 black/white e-paper,
ESP32-S3 with 8 MB PSRAM, speaker, micro-SD slot, three buttons).

It shows live prices from Yahoo Finance, an intraday chart, the SET index, the
profit/loss of your long and short positions, and rings an alarm when a
position reaches its target price. Everything is configured from a web page
served by the ticker itself.

Contents

1. [Hardware](#1-hardware)
2. [Building the firmware](#2-building-the-firmware)
3. [Flashing](#3-flashing)
4. [First-time setup](#4-first-time-setup)
5. [Daily use](#5-daily-use)
6. [The settings page](#6-the-settings-page)
7. [SD card](#7-sd-card)
8. [Updating the firmware](#8-updating-the-firmware)
9. [HTTP API](#9-http-api)
10. [Troubleshooting](#10-troubleshooting)
11. [Source layout](#11-source-layout)

---

## 1. Hardware

| Part | Notes |
|---|---|
| Board | Waveshare ESP32-S3-ePaper-3.97 (ESP32-S3R8 with 8 MB octal PSRAM; the build uses the 8 MB flash layout) |
| Display | 3.97" 800×480 e-paper, SSD1677 controller (SPI) |
| Audio | ES8311 codec + NS4150B amplifier, on-board speaker |
| Storage | micro-SD slot (FAT32 card, optional) |
| Buttons | UP (GPIO 4), MENU (GPIO 5), DOWN (GPIO 6), BOOT (GPIO 0) |
| Power / data | USB-C (native USB, appears as `/dev/ttyACM0` on Linux) |

The board needs 2.4 GHz Wi-Fi with internet access to fetch prices.

---

## 2. Building the firmware

The project uses **PlatformIO** with the Arduino framework (pioarduino
`platform-espressif32` 54.03.20).

### 2.1 Install PlatformIO

Either install the PlatformIO IDE extension for VS Code, or the command-line
tool:

```sh
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
python3 get-platformio.py
# the pio command is then at ~/.platformio/penv/bin/pio
```

On Linux, also install the PlatformIO udev rules so the board can be flashed
without root (see the PlatformIO documentation, "99-platformio-udev.rules").

The first build downloads the ESP32 toolchain, the Arduino core and the
libraries listed in `platformio.ini` (Adafruit GFX, U8g2_for_Adafruit_GFX,
ArduinoJson). The ES8311 codec driver is included in `lib/es8311`.

### 2.2 Optional first-boot defaults (`include/config.h`)

`include/config.h` is optional and is git-ignored. Without it, the ticker
starts in setup mode and you configure everything in the browser.
If you want a freshly flashed board to join your network straight away:

```sh
cp include/config.example.h include/config.h
```

and edit it:

```c
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define STOCK_LABEL   "SJWD"     // SET symbol shown until you configure stocks
```

The firmware reads only these three macros. Settings saved from the web
portal always override them. The `diag` build requires `config.h` because it
uses `WIFI_SSID`/`WIFI_PASSWORD`.

### 2.3 Set the version

Before each release, update `FW_VERSION` in `platformio.ini`. The settings
page shows it, so you can tell which build is running:

```ini
build_flags =
    ...
    -DFW_VERSION=\"2026.10.13.1\"
```

### 2.4 Build

```sh
cd epaper
~/.platformio/penv/bin/pio run                       # build the ticker (default env)
~/.platformio/penv/bin/pio run -e diag               # build the hardware diagnostic
~/.platformio/penv/bin/pio run -t clean              # clean
```

Build outputs are written to `.pio/build/esp32s3-epaper-397/`:

| File | Use |
|---|---|
| `firmware.bin` | Application image. Use this file for SD-card/portal updates and for flashing at 0x10000. |
| `bootloader.bin` | Second-stage bootloader (flash at 0x0) |
| `partitions.bin` | Partition table (flash at 0x8000) |
| `firmware.elf` | For decoding crash backtraces |

---

## 3. Flashing

### 3.1 Over USB with PlatformIO (normal method)

Connect the board with USB-C and run:

```sh
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/ttyACM0
```

If the port does not show up or the upload cannot connect, put the board into
download mode: hold **BOOT**, tap **RESET** (or plug in USB), and release
**BOOT**. Press RESET again after flashing.

Open the serial log (115200 baud):

```sh
~/.platformio/penv/bin/pio device monitor -b 115200
```

### 3.2 Pre-built merged image

`sjwd-ticker-full.bin` is a single merged image that contains the bootloader,
the partition table and the app. Flash it at offset **0x0** with esptool. You
do not need PlatformIO for this:

```sh
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x0 sjwd-ticker-full.bin
```

This image comes from an earlier build and can be older than the source tree.
To make a fresh merged image after `pio run`:

```sh
B=.pio/build/esp32s3-epaper-397
esptool.py --chip esp32s3 merge_bin -o sjwd-ticker-full.bin \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0x10000 $B/firmware.bin
```

(`esptool.py` is installed with PlatformIO, at
`~/.platformio/packages/tool-esptoolpy/esptool.py`.)

### 3.3 Hardware diagnostic

```sh
~/.platformio/penv/bin/pio run -e diag -t upload && ~/.platformio/penv/bin/pio device monitor
```

This build runs four full-screen refreshes (black/white, at 4 MHz and 10 MHz
SPI, with Wi-Fi off and connecting), then prints `done` or `STUCK BUSY` for
each one. Use it when the panel stays blank. Flash the normal firmware again
afterwards.

---

## 4. First-time setup

1. Power on the board. With no Wi-Fi configured, it shows the **setup
   screen**: a hotspot name `StockTicker-XXXX`, its password, a QR code and
   an address.
2. Connect your phone or computer to the `StockTicker-XXXX` Wi-Fi. The default
   password is **`stockticker`**. You can also scan the QR code.
3. The settings page usually opens by itself (captive portal). If it does not,
   browse to **http://192.168.4.1**.
4. Enter at least one Wi-Fi network and one stock (see [section 6](#6-the-settings-page)),
   then press **Save & restart**.
5. The ticker joins your network, sets its clock from NTP (Thailand time,
   UTC+7) and draws its first quote within about a minute.

From now on, the settings page is available at the ticker's own IP address on
your network (`http://<ticker IP>`). The IP is printed in the screen footer.

---

## 5. Daily use

### 5.1 The screen

- **Header:** symbol, company name, page dots (one per stock), an `ALARM`
  badge while ringing, and MARKET OPEN / MARKET CLOSED / HOLIDAY: CLOSED.
- **Price panel:** the large price and the change from the previous close,
  day high/low, 52-week high/low, volume, and the SET index.
- **Intraday chart:** 1-minute closes for the session, with a line for each
  position's target price.
- **Positions table:** per position: shares, entry price, target, distance to
  target, P/L now (THB and %), and P/L at target. Rows whose target has
  been reached are drawn inverted (white on black).
- **Strip:** every configured stock with its price, change, total P/L and the
  time it was last updated.
- **Footer:** last update time, total portfolio P/L, and the settings URL.

The panel uses fast partial refreshes. Every 30th refresh is a full,
flashing refresh that clears ghosting.

Prices update every *Update prices* seconds (default 60). The trading hours
are the SET continuous sessions: Monday–Friday 10:00–12:30 and 14:30–16:40,
excluding the holidays in the holiday list. Outside those hours the
last price stays on screen and the header shows MARKET CLOSED.

### 5.2 Buttons

| Button | Normal mode | Setup mode |
|---|---|---|
| UP | Previous stock | – |
| DOWN | Next stock | – |
| MENU (click) | Silence alarm | Leave setup mode without saving (restart) |
| MENU (hold 3 s) | Restart into setup mode | – |
| Any button | Silences a ringing alarm; it does nothing else while ringing | – |
| MENU or BOOT held at power-on | Start directly in setup mode | – |

If *Switch stock page* is non-zero, the pages also rotate automatically. Rotation pauses
while the alarm rings.

### 5.3 Positions and the alarm

Each stock can have up to three positions:

| Side | Entry price | Target price | Target reached when |
|---|---|---|---|
| **Long** | price you bought at | price you want to sell at | price ≥ target |
| **Short** | price you sold at | price you want to buy back at | price ≤ target |

P/L is calculated exactly in satang (0.01 THB), so large share counts do not
pick up rounding errors:
`long P/L = (price − entry) × shares`, `short P/L = (entry − price) × shares`.

When a position first **crosses** into its target, the ticker:

1. switches to that stock's page,
2. inverts the position row and shows the `ALARM` badge,
3. rings for *Ring for* seconds (default 30) at the set volume, unless the alarm is
   disabled.

Press any button to silence it. The alarm does not ring again for the same
position until the price moves back out of the target and crosses in again.
The sound is a custom WAV from the SD card if one matches
([section 7](#7-sd-card)); otherwise it is a built-in triple beep.

### 5.4 Setup mode later on

You can return to setup mode at any time: hold MENU for 3 s, or press **Start
setup hotspot** on the settings page. If the ticker is connected to Wi-Fi it
stays connected while the hotspot runs, so **Check** can still look up
symbols online.

If none of the saved networks can be joined (45 s timeout), the ticker enters
setup mode by itself. If nobody connects to the hotspot for 10 minutes, it
restarts and tries the saved networks again. A power cut or router reboot
therefore recovers without any action from you.

---

## 6. The settings page

Open `http://<ticker IP>` (normal mode) or `http://192.168.4.1` (setup
hotspot). Nothing changes until you press **Save & restart**. Invalid input is
rejected with a message that names the problem field.

### Wi-Fi networks
Up to 4 networks. The ticker joins the strongest one available. **Scan for
networks** lists nearby SSIDs. Saved passwords are never sent back to the
browser: leave the field blank to keep the stored password. Passwords must
be empty (open network) or at least 8 characters.

### Stocks (SET Thailand)
Up to 3 symbols. Enter the plain SET symbol (`SJWD`, `PTT`, `CPALL`, …). The
`.BK` suffix is optional and is removed. **Check** looks the symbol up on Yahoo
Finance and shows the company name and price. Symbols that are not on the SET
exchange are rejected.

Each stock has up to 3 position rows:

| Field | Required | Notes |
|---|---|---|
| Ref # | no | Your own reference, ≤ 20 characters (portal only) |
| Entry date | no | `YYYY-MM-DD`; the page shows how long you have held it |
| Side | yes | Long or Short |
| Shares | yes | Positive whole number |
| Entry price | yes | Bought at (long) / sold at (short) |
| Target price | no | Sell at (long) / buy back at (short); empty = no alarm |

The *Total amount* column shows shares × price, and the summary row totals each
stock. Empty rows are ignored.

### Alarm
On/off, volume (0–100), ring duration (3–600 s). **Test alarm** plays a 3-second
beep at the slider volume.

### Display
- *Update prices every*: 30–3600 s (default 60).
- *Switch stock page every*: 0–3600 s (default 20; 0 = never rotate).

### SD card
Backup and restore, alarm sounds (upload, play ▶, delete ✕), the market holiday
editor, and firmware upload/install. See [sections 7](#7-sd-card) and [8](#8-updating-the-firmware).

### Setup hotspot
Change the hotspot password (≥ 8 characters; leave blank to keep the current
one), or start the hotspot now.

Settings are stored in the board's flash (NVS) and survive power cycles and
firmware updates.

---

## 7. SD card

The card is optional. Use a FAT32 micro-SD card. The ticker creates a
`/ticker` folder and keeps all its files there. You can edit the files on a
computer, or manage them from the settings page.

| Path | Purpose |
|---|---|
| `/ticker/settings.json` | Settings backup, rewritten on every save. **Contains Wi-Fi passwords**, so keep the card safe. "Restore from card" applies it. |
| `/ticker/restore.json` | Put a settings file here and it is applied at the next power-on, then renamed `restore-applied.json` (or `restore-failed.json`). |
| `/ticker/sounds/*.wav` | Custom alarm sounds |
| `/ticker/holidays.txt` | SET market holidays |
| `/ticker/firmware.bin` | Firmware to install at the next power-on ([section 8](#8-updating-the-firmware)) |

### Cloning a ticker / settings file format
Copy `settings.json` from one ticker to another card as `restore.json`, then
power on. The format is:

```json
{
  "wifi": [ { "ssid": "HomeWiFi", "pass": "secret123" } ],
  "stocks": [
    { "symbol": "SJWD",
      "pos": [
        { "side": "long",  "entry": 9.35, "target": 10.5, "volume": 50000,
          "ref": "A-001", "date": "2026-09-01" },
        { "side": "short", "entry": 11.2, "target": 9.8,  "volume": 10000 }
      ] }
  ],
  "alarmOn": true, "alarmVolume": 70, "alarmSeconds": 30,
  "fetchSeconds": 60, "rotateSeconds": 20,
  "apPass": "stockticker"
}
```

### Alarm sounds
Use PCM WAV files: 8- or 16-bit, mono or stereo, 8–48 kHz. Shorter sounds
loop for the alarm duration. For each alarm the ticker uses the first file
that exists:

1. `<SYMBOL>-long.wav` or `<SYMBOL>-short.wav` (for example `SJWD-long.wav`)
2. `<SYMBOL>.wav`
3. `long.wav` or `short.wav`
4. `alarm.wav`
5. otherwise, the built-in beep

Uploaded file names may contain only letters, digits, `- _ . &`, up to 40
characters.

### Market holidays
Write one holiday per line as `YYYY-MM-DD  Name`. Lines starting with `#` are
comments. The list holds up to 120 entries. On first use with a card, the
official SET 2026 calendar is written to the card; without a card that
built-in list is used. **Add next year's holidays before January**, either
in the portal editor or by editing the file.

---

## 8. Updating the firmware

Build with `pio run` (update `FW_VERSION` first). Then install
`.pio/build/esp32s3-epaper-397/firmware.bin` in one of these ways:

1. **Over USB:** `pio run -t upload` ([section 3](#3-flashing)).
2. **From the settings page:** SD card → Firmware → choose `firmware.bin` →
   **Upload firmware** → **Install now**. The ticker restarts and installs it.
3. **From the SD card directly:** copy the file to the card as
   `/ticker/firmware.bin` and power on.

During an SD update the screen shows the progress. **Do not power off.** The
image is checked first (ESP32 magic byte, size ≥ 64 KB) and verified after
writing. Afterwards the file is renamed to `firmware-installed.bin` or
`firmware-failed.bin`, so it is never installed twice. A failed update leaves
the previous firmware running. The installed version is shown under SD card →
Firmware.

Use `firmware.bin`, not the merged `sjwd-ticker-full.bin`, for SD and portal
updates.

---

## 9. HTTP API

The settings page uses these endpoints. You can also use them for scripting.
They have no authentication, so keep the ticker on a trusted network.

| Method | Path | Description |
|---|---|---|
| GET | `/` | Settings page |
| GET | `/api/config` | Current settings (passwords omitted) |
| POST | `/api/config` | Save settings (JSON body as in [section 7](#7-sd-card); a Wi-Fi entry with `"keep": true` and no `pass` keeps the stored password). Restarts. |
| GET | `/api/status` | Hotspot/station state, SSID, IP |
| GET | `/api/scan` | Nearby Wi-Fi networks |
| GET | `/api/lookup?symbol=SJWD` | Check a SET symbol (name, price) |
| POST | `/api/test-alarm?volume=70` | 3-second test beep |
| POST | `/api/setup-mode` | Restart into setup mode |
| GET | `/screenshot.pbm` | Current screen as an 800×480 PBM image |
| GET | `/api/sd` | Card status, firmware version, sounds, holiday source |
| POST | `/api/sd/backup` / `/api/sd/restore` | Back up / restore `settings.json` |
| POST | `/api/sd/play?file=X.wav` / `/api/sd/delete-sound?file=X.wav` | Play / delete a sound |
| POST | `/api/sd/upload?kind=sound\|firmware` | Multipart file upload to the card |
| POST | `/api/sd/install-firmware` | Restart and install `/ticker/firmware.bin` |
| GET / POST | `/api/holidays` | Read / replace the holiday list (plain text) |

Example: take a screenshot and convert it to PNG:

```sh
curl -o screen.pbm http://<ticker IP>/screenshot.pbm && convert screen.pbm screen.png
```

---

## 10. Troubleshooting

| Symptom | What to do |
|---|---|
| Setup screen says "Could not connect to: …" | Check the SSID/password (2.4 GHz only). The ticker retries every 10 minutes when nobody is using the hotspot. |
| Settings page does not open on the hotspot | Browse to `http://192.168.4.1` manually. Turn off mobile data on phones that ignore captive portals. |
| A stock shows an error / stale time in the strip | Yahoo Finance is unreachable, or the symbol is wrong. The ticker keeps the last good data and retries at the next update. Watch the serial log for `fetch failed:`. |
| "Not a SET stock" / "Unknown symbol" | Only SET-listed symbols are accepted. Enter them without `.BK`. |
| Screen shows "closed" on a trading day | The date is in `holidays.txt`, or the clock is wrong (NTP blocked). |
| Faint ghost images | Normal for e-paper. They are cleared by the full refresh every 30 updates, or by restarting. |
| Display stays blank / "EPD busy timeout" in the log | Flash the `diag` environment and check its output ([section 3.3](#33-hardware-diagnostic)). |
| No sound | Check that the alarm is enabled and the volume is above 0, and try **Test alarm**. An unreadable WAV falls back to the beep. |
| Firmware update failed | See the message on screen. The old firmware keeps running, and the file is now `firmware-failed.bin`. Re-copy a proper `firmware.bin`. |
| Upload fails: port busy / no device | Close any open serial monitor. Enter download mode (hold BOOT, tap RESET). |
| Forgot hotspot password | Re-flash with USB, or put a `restore.json` with a new `apPass` on the card. |

Serial log: `pio device monitor -b 115200`. The log shows Wi-Fi, quote, SD,
holiday, alarm and target events.

---

## 11. Source layout

```
platformio.ini          build environments (ticker + diag), FW_VERSION, libraries
include/config.example.h  optional first-boot defaults (copy to config.h)
src/main.cpp            setup/loop, buttons, modes, fetch schedule, alarm trigger
src/settings.*          settings model, JSON (de)serialisation + validation, NVS storage
src/portfolio.h         long/short P/L maths in satang, target detection
src/quotes.*            Yahoo Finance chart API client (stocks + ^SET index)
src/market.*            SET trading hours and holiday calendar
src/screen.*            800x480 layouts (ticker, setup, messages), refresh policy
src/epd397.h            minimal SSD1677 panel driver (full + partial refresh)
src/portal.* portal_html.h   web server, captive portal, HTTP API, settings page
src/alarm.*             ES8311/I2S audio task: beep and WAV playback
src/storage.*           SD card (SD_MMC) helpers and alarm sound lookup
src/firmware.*          firmware install from SD card
lib/es8311/             ES8311 codec driver
diag/diag.cpp           display/Wi-Fi hardware diagnostic
sjwd-ticker-full.bin    pre-built merged flash image (flash at 0x0)
```

Prices come from Yahoo Finance's public chart endpoint
(`query1.finance.yahoo.com/v8/finance/chart/<SYMBOL>.BK`). They may be delayed
and are for information only.
