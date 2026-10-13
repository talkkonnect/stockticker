#include "alarm.h"

#include <ESP_I2S.h>
#include <Wire.h>
#include <SD_MMC.h>
#include "es8311.h"

// Board wiring (ESP32-S3-ePaper-3.97).
constexpr int I2C_SDA = 41, I2C_SCL = 42;
constexpr int I2S_MCK = 13, I2S_BCK = 14, I2S_LRCK = 47, I2S_DOUT = 48, I2S_DIN = 21;
constexpr int PA_CTRL = 39;  // speaker amplifier enable

constexpr int BEEP_RATE = 24000;
constexpr int CHUNK_MS = 25;
constexpr int CHUNK_SAMPLES = 48000 * CHUNK_MS / 1000;  // sized for the highest rate

static I2SClass i2s;
static es8311_handle_t codec = nullptr;
static bool ready = false;
static int currentRate = 0;

// Request from other tasks, picked up by the alarm task.
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t ringUntil = 0;
static volatile int ringVolume = 70;
static volatile uint32_t requestId = 0;
static char requestedWav[128] = "";

static int16_t buf[CHUNK_SAMPLES];

static bool stillRinging() { return (int32_t)(ringUntil - millis()) > 0; }

static void setRate(int rate) {
  if (rate == currentRate) return;
  i2s.configureTX(rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  es8311_sample_frequency_config(codec, rate * 256, rate);
  currentRate = rate;
}

// Classic alarm-clock pattern: three short beeps, then a pause.
static void playBeep(uint32_t id) {
  setRate(BEEP_RATE);
  const int samples = BEEP_RATE * CHUNK_MS / 1000;
  const float step = 2.0f * PI * 2000.0f / BEEP_RATE;  // 2 kHz tone
  float phase = 0;
  while (stillRinging() && requestId == id) {
    uint32_t t = millis() % 1100;
    bool on = t < 450 && (t % 150) < 100;
    for (int i = 0; i < samples; i++) {
      buf[i] = on ? (int16_t)(sinf(phase) * 12000) : 0;
      phase += step;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s.write((uint8_t *)buf, samples * 2);  // blocks ~CHUNK_MS
  }
}

struct WavInfo {
  int rate = 0, channels = 0, bits = 0;
  uint32_t dataStart = 0, dataSize = 0;
};

static uint32_t readLE(File &f, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) v |= (uint32_t)f.read() << (8 * i);
  return v;
}

// Walks the RIFF chunks; accepts 8/16-bit PCM, mono or stereo, 8-48 kHz.
static bool parseWav(File &f, WavInfo &w) {
  char tag[5] = {0};
  if (f.read((uint8_t *)tag, 4) != 4 || strcmp(tag, "RIFF")) return false;
  readLE(f, 4);
  if (f.read((uint8_t *)tag, 4) != 4 || strcmp(tag, "WAVE")) return false;
  bool haveFmt = false;
  while (f.available() >= 8) {
    f.read((uint8_t *)tag, 4);
    uint32_t size = readLE(f, 4);
    uint32_t next = f.position() + size + (size & 1);
    if (!strcmp(tag, "fmt ")) {
      int format = readLE(f, 2);
      w.channels = readLE(f, 2);
      w.rate = readLE(f, 4);
      readLE(f, 6);
      w.bits = readLE(f, 2);
      haveFmt = format == 1;  // PCM only
    } else if (!strcmp(tag, "data")) {
      w.dataStart = f.position();
      w.dataSize = size;
      break;
    }
    f.seek(next);
  }
  return haveFmt && w.dataSize > 0 && (w.channels == 1 || w.channels == 2) && (w.bits == 8 || w.bits == 16) &&
         w.rate >= 8000 && w.rate <= 48000;
}

// Loops the file until the alarm ends. Returns false if it can't be played.
static bool playWav(const char *path, uint32_t id) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("Alarm: cannot open %s\n", path);
    return false;
  }
  WavInfo w;
  if (!parseWav(f, w)) {
    Serial.printf("Alarm: %s is not 8/16-bit PCM WAV (8-48 kHz)\n", path);
    f.close();
    return false;
  }
  setRate(w.rate);
  const int frameBytes = w.channels * w.bits / 8;
  static uint8_t raw[CHUNK_SAMPLES * 4];
  uint32_t pos = 0;
  f.seek(w.dataStart);
  while (stillRinging() && requestId == id) {
    if (pos >= w.dataSize) {  // loop
      pos = 0;
      f.seek(w.dataStart);
    }
    size_t want = min<uint32_t>((uint32_t)(w.rate * CHUNK_MS / 1000) * frameBytes, w.dataSize - pos);
    want -= want % frameBytes;
    int got = f.read(raw, want);
    if (got <= 0) break;
    pos += got;
    int frames = got / frameBytes;
    for (int i = 0; i < frames; i++) {  // to 16-bit mono
      int32_t s;
      if (w.bits == 16) {
        const int16_t *p = (const int16_t *)(raw + i * frameBytes);
        s = w.channels == 2 ? ((int32_t)p[0] + p[1]) / 2 : p[0];
      } else {
        const uint8_t *p = raw + i * frameBytes;
        s = w.channels == 2 ? (((int32_t)p[0] + p[1]) / 2 - 128) << 8 : ((int32_t)p[0] - 128) << 8;
      }
      buf[i] = (int16_t)s;
    }
    i2s.write((uint8_t *)buf, frames * 2);
  }
  f.close();
  return true;
}

static void alarmTask(void *) {
  for (;;) {
    if (!stillRinging()) {
      digitalWrite(PA_CTRL, LOW);
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    uint32_t id;
    char wav[sizeof(requestedWav)];
    int volume;
    portENTER_CRITICAL(&mux);
    id = requestId;
    strcpy(wav, requestedWav);
    volume = ringVolume;
    portEXIT_CRITICAL(&mux);

    es8311_voice_volume_set(codec, volume, nullptr);
    digitalWrite(PA_CTRL, HIGH);
    if (!wav[0] || !playWav(wav, id)) playBeep(id);
    // Flush with silence so the amplifier doesn't click on stale samples.
    memset(buf, 0, sizeof(buf));
    i2s.write((uint8_t *)buf, (currentRate * CHUNK_MS / 1000) * 2);
    if (!stillRinging()) digitalWrite(PA_CTRL, LOW);
  }
}

void alarmBegin() {
  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, LOW);
  Wire.begin(I2C_SDA, I2C_SCL);

  codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
  const es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = BEEP_RATE * 256,
      .sample_frequency = BEEP_RATE,
  };
  if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
    Serial.println("Alarm: ES8311 init failed");
    return;
  }
  es8311_microphone_config(codec, false);

  i2s.setPins(I2S_BCK, I2S_LRCK, I2S_DOUT, I2S_DIN, I2S_MCK);
  if (!i2s.begin(I2S_MODE_STD, BEEP_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("Alarm: I2S init failed");
    return;
  }
  currentRate = BEEP_RATE;
  xTaskCreatePinnedToCore(alarmTask, "alarm", 6144, nullptr, 2, nullptr, 0);
  ready = true;
}

void alarmRing(int seconds, int volume, const String &wavPath) {
  if (!ready) return;
  portENTER_CRITICAL(&mux);
  strlcpy(requestedWav, wavPath.c_str(), sizeof(requestedWav));
  ringVolume = constrain(volume, 0, 100);
  ringUntil = millis() + (uint32_t)seconds * 1000;
  requestId++;  // interrupts whatever is playing so the new sound starts
  portEXIT_CRITICAL(&mux);
}

void alarmSilence() { ringUntil = millis(); }

bool alarmRinging() { return ready && stillRinging(); }
