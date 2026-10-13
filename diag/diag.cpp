// Hardware diagnostic: does the e-paper refresh complete under different conditions?
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include "config.h"

constexpr int P_BUSY = 3, P_DC = 9, P_CS = 10, P_SCLK = 11, P_MOSI = 12, P_RST = 46;
uint32_t spiHz = 4000000;

void cmd(uint8_t c) { SPI.beginTransaction(SPISettings(spiHz, MSBFIRST, SPI_MODE0)); digitalWrite(P_DC, LOW); digitalWrite(P_CS, LOW); SPI.transfer(c); digitalWrite(P_CS, HIGH); SPI.endTransaction(); }
void dat(uint8_t d) { SPI.beginTransaction(SPISettings(spiHz, MSBFIRST, SPI_MODE0)); digitalWrite(P_DC, HIGH); digitalWrite(P_CS, LOW); SPI.transfer(d); digitalWrite(P_CS, HIGH); SPI.endTransaction(); }

void refreshTest(const char *label, uint8_t pattern) {
  digitalWrite(P_RST, LOW); delay(10); digitalWrite(P_RST, HIGH); delay(20);
  cmd(0x12); delay(20);
  cmd(0x18); dat(0x80);
  cmd(0x0C); dat(0xAE); dat(0xC7); dat(0xC3); dat(0xC0); dat(0x80);
  cmd(0x01); dat(0xDF); dat(0x01); dat(0x02);
  cmd(0x3C); dat(0x01);
  cmd(0x11); dat(0x01);
  cmd(0x44); dat(0); dat(0); dat(0x1F); dat(0x03);
  cmd(0x45); dat(0xDF); dat(0x01); dat(0); dat(0);
  cmd(0x4E); dat(0); dat(0); cmd(0x4F); dat(0); dat(0);
  cmd(0x24);
  SPI.beginTransaction(SPISettings(spiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(P_DC, HIGH); digitalWrite(P_CS, LOW);
  for (int i = 0; i < 800 * 480 / 8; i++) SPI.transfer(pattern);
  digitalWrite(P_CS, HIGH);
  SPI.endTransaction();
  cmd(0x22); dat(0xF7); cmd(0x20);
  uint32_t t0 = millis(), lowSince = 0, highTotal = 0, lastT = millis();
  while (millis() - t0 < 15000) {
    int b = digitalRead(P_BUSY);
    if (b == LOW) { if (!lowSince) lowSince = millis(); if (millis() - lowSince > 300) break; }
    else { lowSince = 0; }
    delayMicroseconds(100);
  }
  uint32_t el = millis() - t0;
  Serial.printf("TEST %-34s %s in %lu ms\n", label, el >= 15000 ? "STUCK BUSY" : "done", el > 300 ? el - 300 : el);
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("\n===== DIAG START =====");
  pinMode(P_BUSY, INPUT);
  pinMode(P_RST, OUTPUT); pinMode(P_DC, OUTPUT); pinMode(P_CS, OUTPUT);
  digitalWrite(P_CS, HIGH); digitalWrite(P_RST, HIGH);
  SPI.begin(P_SCLK, -1, P_MOSI, -1);

  refreshTest("4MHz, no wifi (black)", 0x00);
  spiHz = 10000000;
  refreshTest("10MHz, no wifi (white)", 0xFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  delay(1000);
  refreshTest("10MHz, wifi connecting (black)", 0x00);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);
  refreshTest("10MHz, wifi off again (white)", 0xFF);
  Serial.println("===== DIAG END =====");
}

void loop() { delay(1000); }
