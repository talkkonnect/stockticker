#pragma once
// Minimal SSD1677 driver for the Waveshare ESP32-S3-ePaper-3.97 (800x480).
// Follows Waveshare's own EPD_3in97 init/refresh sequence, which is known to
// work on this board (GxEPD2's sequence leaves the panel stuck busy here).
// Frames are 1 bpp, MSB = leftmost pixel, bit set = white — the same layout
// as an Adafruit GFXcanvas1, so a canvas buffer can be sent as-is.

#include <Arduino.h>
#include <SPI.h>

class Epd397 {
 public:
  static constexpr int WIDTH = 800;
  static constexpr int HEIGHT = 480;
  static constexpr size_t FRAME_BYTES = WIDTH / 8 * HEIGHT;

  Epd397(int cs, int dc, int rst, int busy, int sclk, int mosi)
      : _cs(cs), _dc(dc), _rst(rst), _busy(busy), _sclk(sclk), _mosi(mosi) {}

  void begin() {
    pinMode(_busy, INPUT);
    pinMode(_rst, OUTPUT);
    pinMode(_dc, OUTPUT);
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    digitalWrite(_rst, HIGH);
    SPI.begin(_sclk, -1, _mosi, -1);
    _prev = (uint8_t *)malloc(FRAME_BYTES);
    if (_prev) memset(_prev, 0xFF, FRAME_BYTES);
  }

  // Full (flashing) refresh: clears ghosting. ~2.2 s.
  void displayFull(const uint8_t *frame) {
    _reset();
    _waitBusy();
    _cmd(0x12);  // SWRESET
    _waitBusy();
    _cmd(0x18); _data(0x80);  // internal temperature sensor
    _cmd(0x0C); _data(0xAE); _data(0xC7); _data(0xC3); _data(0xC0); _data(0x80);  // booster
    _cmd(0x01); _data((HEIGHT - 1) % 256); _data((HEIGHT - 1) / 256); _data(0x02);  // driver output
    _cmd(0x3C); _data(0x01);  // border waveform
    _setRamArea();
    _waitBusy();
    _writeRam(0x24, frame);
    _writeRam(0x26, frame);
    _cmd(0x22); _data(0xF7);
    _cmd(0x20);
    _waitBusy();
    _remember(frame);
    _sleep();
  }

  // Partial refresh of the whole screen: no flashing. ~0.5 s.
  void displayPartial(const uint8_t *frame) {
    _reset();  // wake from deep sleep; RAM and settings are retained
    _cmd(0x18); _data(0x80);
    _cmd(0x3C); _data(0x80);
    _setRamArea();
    _writeRam(0x24, frame);                    // new image
    _writeRam(0x26, _prev ? _prev : frame);    // previous image, for the diff waveform
    _cmd(0x22); _data(0xFF);
    _cmd(0x20);
    _waitBusy();
    _remember(frame);
    _sleep();
  }

 private:
  int _cs, _dc, _rst, _busy, _sclk, _mosi;
  uint8_t *_prev = nullptr;
  const SPISettings _spi{10000000, MSBFIRST, SPI_MODE0};

  void _reset() {
    digitalWrite(_rst, HIGH); delay(20);
    digitalWrite(_rst, LOW); delay(2);
    digitalWrite(_rst, HIGH); delay(20);
  }

  void _cmd(uint8_t c) {
    SPI.beginTransaction(_spi);
    digitalWrite(_dc, LOW); digitalWrite(_cs, LOW);
    SPI.transfer(c);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
  }

  void _data(uint8_t d) {
    SPI.beginTransaction(_spi);
    digitalWrite(_dc, HIGH); digitalWrite(_cs, LOW);
    SPI.transfer(d);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
  }

  void _writeRam(uint8_t reg, const uint8_t *buf) {
    _cmd(reg);
    SPI.beginTransaction(_spi);
    digitalWrite(_dc, HIGH); digitalWrite(_cs, LOW);
    SPI.writeBytes(buf, FRAME_BYTES);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
  }

  // Gates are reversed on this panel: X increments, Y decrements from the last row.
  void _setRamArea() {
    _cmd(0x11); _data(0x01);
    _cmd(0x44); _data(0); _data(0); _data((WIDTH - 1) % 256); _data((WIDTH - 1) / 256);
    _cmd(0x45); _data((HEIGHT - 1) % 256); _data((HEIGHT - 1) / 256); _data(0); _data(0);
    _cmd(0x4E); _data(0); _data(0);
    _cmd(0x4F); _data((HEIGHT - 1) % 256); _data((HEIGHT - 1) / 256);
  }

  void _sleep() {
    _cmd(0x10); _data(0x01);  // deep sleep; next refresh starts with a HW reset
  }

  void _remember(const uint8_t *frame) {
    if (_prev && _prev != frame) memcpy(_prev, frame, FRAME_BYTES);
  }

  // BUSY chatters at high frequency while refreshing, so require it to stay
  // LOW for a while before treating the panel as idle.
  void _waitBusy() {
    delay(2);
    const uint32_t start = millis();
    uint32_t lowSince = 0;
    while (millis() - start < 10000) {
      if (digitalRead(_busy) == LOW) {
        if (!lowSince) lowSince = millis();
        if (millis() - lowSince >= 30) return;
      } else {
        lowSince = 0;
      }
      delayMicroseconds(200);
    }
    Serial.println("EPD busy timeout");
  }
};
