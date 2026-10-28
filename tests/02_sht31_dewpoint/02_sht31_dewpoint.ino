// 02_sht31_dewpoint — SHT31 A (0x44) and B (0x45) on hardware I2C (D20/D21, 50 kHz),
// SHT30 room sensor (0x44) on software I2C (SDA2 D34, SCL2 D36).
// Prints T, RH, dew point and NACK/CRC counts, plus DP max and spread.
// Bench check 4 (rev 2): all three in the same air; DPs should agree within ~1 °C.
//
// Buttons:
//   BTN1 short  reset error counters
//   BTN2 short  toggle read rate 2 s / 0.5 s (0.5 s stresses the long bus)
//   BTN1 long   breath test marker: prints "--- breathe on a sensor now ---" (DP of that one should jump)
// LED: green flash per read with all valid, yellow if one missing, red if none.

#include <Wire.h>
#include "softi2c.h"
#include "bench.h"

SoftI2C bus2(34, 36);

// RH calibration offsets (% RH, as config.h SHT_RH_OFFSET_*): A read low, B high. Set 0 to see raw readings.
struct Dev { const char *name; uint8_t addr; bool soft; float rhOfs; uint16_t nack, crc; };
Dev devs[3] = {{"A 0x44", 0x44, false, 1.15f, 0, 0}, {"B 0x45", 0x45, false, -1.15f, 0, 0}, {"R soft", 0x44, true, 0.0f, 0, 0}};
uint16_t periodMs = 2000;

uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

bool trigger(Dev &d) {
  const uint8_t cmd[2] = {0x24, 0x00};   // single shot, high repeatability, no clock stretching
  if (d.soft) return bus2.write(d.addr, cmd, 2);
  Wire.beginTransmission(d.addr);
  Wire.write(cmd, 2);
  return Wire.endTransmission() == 0;
}

bool fetch(Dev &d, uint8_t *b) {
  if (d.soft) return bus2.read(d.addr, b, 6);
  if (Wire.requestFrom(d.addr, (uint8_t)6) != 6) return false;
  for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
  return true;
}

float dewPoint(float t, float rh) {
  const float a = 17.62f, b = 243.12f;
  float g = log(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

void readAll() {
  bool trig[3];
  for (uint8_t i = 0; i < 3; i++) trig[i] = trigger(devs[i]);
  delay(20);
  float dpMax = NAN, dpMin = NAN;
  uint8_t valid = 0;
  for (uint8_t i = 0; i < 3; i++) {
    Dev &d = devs[i];
    uint8_t b[6];
    Serial.print(d.name); Serial.print(F(": "));
    if (!trig[i] || !fetch(d, b)) { d.nack++; Serial.print(F("NACK")); }
    else if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) { d.crc++; Serial.print(F("CRC FAIL")); }
    else {
      float t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
      float rh = constrain(100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f + d.rhOfs, 0.0f, 100.0f);
      float dp = dewPoint(t, rh);
      Serial.print(t, 2); Serial.print(F(" C ")); Serial.print(rh, 1); Serial.print(F(" % DP "));
      Serial.print(dp, 2);
      if (valid == 0 || dp > dpMax) dpMax = dp;
      if (valid == 0 || dp < dpMin) dpMin = dp;
      valid++;
    }
    Serial.print(F(" [")); Serial.print(d.nack); Serial.print('/'); Serial.print(d.crc); Serial.print(F("]   "));
  }
  Serial.print(F("| DP max ")); Serial.print(dpMax, 2);
  Serial.print(F(" spread ")); Serial.print(valid >= 2 ? dpMax - dpMin : 0, 2);
  if (valid >= 2 && dpMax - dpMin > 2.0f) Serial.print(F(" > 2 C ALARM"));
  Serial.println();
  if (valid == 3) benchLed(0, 60, 0); else if (valid) benchLed(120, 70, 0); else benchLed(150, 0, 0);
  delay(40);
  benchLed(0, 0, 0);
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(50000);
  Wire.setWireTimeout(5000, true);
  bus2.begin();
  benchBanner(F("02_sht31_dewpoint"));
  Serial.println(F("BTN1 short: reset counters | BTN2 short: 2 s / 0.5 s | BTN1 long: breath marker"));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  if (btn1.shortPress) {
    for (Dev &d : devs) d.nack = d.crc = 0;
    Serial.println(F("--- counters reset ---"));
  }
  if (btn2.shortPress) {
    periodMs = periodMs == 2000 ? 500 : 2000;
    Serial.print(F("--- period ")); Serial.print(periodMs); Serial.println(F(" ms ---"));
  }
  if (btn1.longPress) Serial.println(F("--- breathe on a sensor now ---"));

  static uint32_t last;
  if (millis() - last >= periodMs) {
    last = millis();
    readAll();
  }
}
