#pragma once
// Copy of firmware/controller/softi2c.h — keep in sync.
#include <Arduino.h>

// Minimal bit-banged I2C master (open-drain emulation, ~25 kHz) for the SHT30 on D34/D36.
// No timers, no interrupts; every wait on SCL has a timeout.
class SoftI2C {
 public:
  SoftI2C(uint8_t sda, uint8_t scl) : _sda(sda), _scl(scl) {}
  void begin();
  bool write(uint8_t addr, const uint8_t *data, uint8_t n);
  bool read(uint8_t addr, uint8_t *data, uint8_t n);

 private:
  uint8_t _sda, _scl;
  void sdaLow();
  void sdaRelease();
  void sclLow();
  bool sclRelease();   // false if a slave holds SCL low too long
  bool start();
  void stop();
  bool writeByte(uint8_t b);
  uint8_t readByte(bool ack);
  void recover();
};
