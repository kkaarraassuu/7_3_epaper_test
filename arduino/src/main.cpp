#include <Arduino.h>
#include <SPI.h>
#include "image_data.h"

namespace {
constexpr uint8_t PIN_BUSY = D7;
constexpr uint8_t PIN_RST = D8;
constexpr uint8_t PIN_DC = D9;
constexpr uint8_t PIN_CS = D10;
constexpr uint16_t WIDTH = 800;
constexpr uint16_t HEIGHT = 480;
constexpr uint32_t IMAGE_BYTES = WIDTH * HEIGHT / 2;

void sendCommand(uint8_t value) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(value);
  digitalWrite(PIN_CS, HIGH);
}

void sendData(uint8_t value) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(value);
  digitalWrite(PIN_CS, HIGH);
}

bool waitReady(const char* stage, uint32_t timeoutMs = 60000) {
  Serial.print(stage);
  const uint32_t start = millis();
  while (digitalRead(PIN_BUSY) == LOW) {
    if (millis() - start >= timeoutMs) {
      Serial.println(": BUSY timeout");
      return false;
    }
    delay(1);
  }
  Serial.println(": ready");
  return true;
}

void resetDisplay() {
  digitalWrite(PIN_RST, HIGH); delay(20);
  digitalWrite(PIN_RST, LOW);  delay(2);
  digitalWrite(PIN_RST, HIGH); delay(20);
}

bool initDisplay() {
  resetDisplay();
  if (!waitReady("reset")) return false;
  delay(30);

  sendCommand(0xAA);
  for (uint8_t v : {0x49, 0x55, 0x20, 0x08, 0x09, 0x18}) sendData(v);
  sendCommand(0x01); sendData(0x3F);
  sendCommand(0x00); sendData(0x5F); sendData(0x69);
  sendCommand(0x03);
  for (uint8_t v : {0x00, 0x54, 0x00, 0x44}) sendData(v);
  sendCommand(0x05);
  for (uint8_t v : {0x40, 0x1F, 0x1F, 0x2C}) sendData(v);
  sendCommand(0x06);
  for (uint8_t v : {0x6F, 0x1F, 0x17, 0x49}) sendData(v);
  sendCommand(0x08);
  for (uint8_t v : {0x6F, 0x1F, 0x1F, 0x22}) sendData(v);
  sendCommand(0x30); sendData(0x03);
  sendCommand(0x50); sendData(0x3F);
  sendCommand(0x60); sendData(0x02); sendData(0x00);
  sendCommand(0x61); // 800 x 480
  sendData(0x03); sendData(0x20); sendData(0x01); sendData(0xE0);
  sendCommand(0x84); sendData(0x01);
  sendCommand(0xE3); sendData(0x2F);
  sendCommand(0x04); // power on
  return waitReady("power on", 30000);
}

void sendImage() {
  sendCommand(0x10);
  for (uint32_t i = 0; i < epaper_image_size; ++i) {
    sendData(pgm_read_byte(&epaper_image[i]));
    if (i % 19200 == 0) {
      Serial.printf("transfer: %lu%%\n", static_cast<unsigned long>(i * 100 / epaper_image_size));
    }
  }
}

bool refreshDisplay() {
  sendCommand(0x04);
  if (!waitReady("refresh power on", 30000)) return false;
  sendCommand(0x06);
  for (uint8_t v : {0x6F, 0x1F, 0x17, 0x49}) sendData(v);
  sendCommand(0x12); sendData(0x00);
  if (!waitReady("display refresh", 60000)) return false;
  sendCommand(0x02); sendData(0x00);
  return waitReady("power off", 30000);
}

void sleepDisplay() {
  sendCommand(0x02); sendData(0x00);
  waitReady("sleep power off", 30000);
  sendCommand(0x07); sendData(0xA5);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Waveshare 7.3inch e-Paper HAT (E) image display");

  if (epaper_image_size != IMAGE_BYTES) {
    Serial.printf("invalid image: %lu bytes (expected %lu)\n",
                  static_cast<unsigned long>(epaper_image_size),
                  static_cast<unsigned long>(IMAGE_BYTES));
    return;
  }

  pinMode(PIN_BUSY, INPUT);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, HIGH);

  SPI.begin(); // Nano ESP32: D11=MOSI, D13=SCK
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  if (!initDisplay()) {
    Serial.println("initialization failed");
    SPI.endTransaction();
    return;
  }
  sendImage();
  if (!refreshDisplay()) {
    Serial.println("refresh failed");
    SPI.endTransaction();
    return;
  }
  sleepDisplay();
  SPI.endTransaction();
  Serial.println("display complete; image remains without power");
}

void loop() { delay(1000); }
