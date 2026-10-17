#include "softi2c.h"

static const uint8_t HALF_US = 20;
static const uint16_t STRETCH_TIMEOUT_US = 1000;

// Never drive a line HIGH: LOW = output low, released = input with pull-up.
// digitalWrite(LOW) before pinMode(OUTPUT) so the pin never glitches high.
void SoftI2C::sdaLow() { digitalWrite(_sda, LOW); pinMode(_sda, OUTPUT); }
void SoftI2C::sdaRelease() { pinMode(_sda, INPUT_PULLUP); }
void SoftI2C::sclLow() { digitalWrite(_scl, LOW); pinMode(_scl, OUTPUT); }

bool SoftI2C::sclRelease() {
  pinMode(_scl, INPUT_PULLUP);
  uint16_t waited = 0;
  while (digitalRead(_scl) == LOW) {
    if (waited >= STRETCH_TIMEOUT_US) return false;
    delayMicroseconds(10);
    waited += 10;
  }
  return true;
}

void SoftI2C::begin() {
  sdaRelease();
  sclRelease();
}

// Clock out up to 9 bits if a slave is holding SDA low.
void SoftI2C::recover() {
  sdaRelease();
  for (uint8_t i = 0; i < 9 && digitalRead(_sda) == LOW; i++) {
    sclLow();
    delayMicroseconds(HALF_US);
    sclRelease();
    delayMicroseconds(HALF_US);
  }
  stop();
}

bool SoftI2C::start() {
  sdaRelease();
  if (!sclRelease()) return false;
  delayMicroseconds(HALF_US);
  if (digitalRead(_sda) == LOW) {
    recover();
    if (digitalRead(_sda) == LOW) return false;
  }
  sdaLow();
  delayMicroseconds(HALF_US);
  sclLow();
  delayMicroseconds(HALF_US);
  return true;
}

void SoftI2C::stop() {
  sdaLow();
  delayMicroseconds(HALF_US);
  sclRelease();
  delayMicroseconds(HALF_US);
  sdaRelease();
  delayMicroseconds(HALF_US);
}

bool SoftI2C::writeByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; i++) {
    if (b & 0x80) sdaRelease(); else sdaLow();
    b <<= 1;
    delayMicroseconds(HALF_US);
    if (!sclRelease()) return false;
    delayMicroseconds(HALF_US);
    sclLow();
  }
  sdaRelease();
  delayMicroseconds(HALF_US);
  if (!sclRelease()) return false;
  bool ack = digitalRead(_sda) == LOW;
  delayMicroseconds(HALF_US);
  sclLow();
  return ack;
}

uint8_t SoftI2C::readByte(bool ack) {
  uint8_t b = 0;
  sdaRelease();
  for (uint8_t i = 0; i < 8; i++) {
    delayMicroseconds(HALF_US);
    sclRelease();
    b = (b << 1) | (digitalRead(_sda) == HIGH ? 1 : 0);
    delayMicroseconds(HALF_US);
    sclLow();
  }
  if (ack) sdaLow(); else sdaRelease();
  delayMicroseconds(HALF_US);
  sclRelease();
  delayMicroseconds(HALF_US);
  sclLow();
  sdaRelease();
  return b;
}

bool SoftI2C::write(uint8_t addr, const uint8_t *data, uint8_t n) {
  if (!start()) return false;
  bool ok = writeByte(addr << 1);
  for (uint8_t i = 0; ok && i < n; i++) ok = writeByte(data[i]);
  stop();
  return ok;
}

bool SoftI2C::read(uint8_t addr, uint8_t *data, uint8_t n) {
  if (!start()) return false;
  bool ok = writeByte((addr << 1) | 1);
  if (ok) {
    for (uint8_t i = 0; i < n; i++) data[i] = readByte(i + 1 < n);
  }
  stop();
  return ok;
}
