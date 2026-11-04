// 15_cal_all — calibrates all six temperature sensors to one another in the same air:
// NTC T1 (A5), T2 (A4), T3 (A3) and SHT31 A (0x44), SHT31 B (0x45), SHT30 R (soft I2C D38/D40).
// Reference: the median of the three SHT averages (factory-calibrated, ±0.2 °C), for temperature and RH.
// Each sensor's offset = reference − its own RAW average, printed as #define lines for config.h.
// NTCs use Beta 3435 / R25 10k / 10k pull-up as the firmware. Raw = no offsets applied anywhere.
//
// Setup: all six in still air, close together (probe tips next to the SHTs), settled ≥ 15 min, nobody near.
// Buttons:
//   BTN1 long   start the 10 min averaging run (LED blue; any button aborts)
//   BTN2 short  print one live line now
// Serial: c = start, a = abort. A live line (raw values) is printed every 2 s.

#include <Wire.h>
#include "softi2c.h"
#include "bench.h"

const uint32_t CAL_S = 600;
const float BETA = 3435.0f, R25 = 10000.0f, RPULL = 10000.0f;
const uint8_t NTC_PIN[3] = {A5, A4, A3};
const char *const NAME[6] = {"T1", "T2", "T3", "A", "B", "R"};

SoftI2C bus2(38, 40);

float curT[6], curRH[6];      // last raw reading (NAN = none); RH only for 3..5
float sumT[6], sumRH[6];
uint16_t n[6];
bool calOn;
uint32_t calStart, lastRead;

uint8_t crc8(const uint8_t *d, uint8_t len) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

float ntcRead(uint8_t pin) {
  analogRead(pin);
  uint32_t s = 0;
  for (uint8_t i = 0; i < 16; i++) s += analogRead(pin);
  float adc = s / 16.0f;
  if (adc < 8 || adc > 1015) return NAN;   // short / open
  float r = RPULL * adc / (1023.0f - adc);
  return 1.0f / (1.0f / 298.15f + log(r / R25) / BETA) - 273.15f;
}

bool shtTrigger(uint8_t k) {   // k 0..2 = A, B, R
  const uint8_t cmd[2] = {0x24, 0x00};
  if (k == 2) return bus2.write(0x44, cmd, 2);
  Wire.beginTransmission(k == 0 ? 0x44 : 0x45);
  Wire.write(cmd, 2);
  return Wire.endTransmission() == 0;
}

bool shtFetch(uint8_t k, float &t, float &rh) {
  uint8_t b[6];
  if (k == 2) {
    if (!bus2.read(0x44, b, 6)) return false;
  } else {
    if (Wire.requestFrom((uint8_t)(k == 0 ? 0x44 : 0x45), (uint8_t)6) != 6) return false;
    for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
  }
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return false;
  t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
  rh = 100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f;
  return true;
}

void readAll() {
  for (uint8_t i = 0; i < 3; i++) curT[i] = ntcRead(NTC_PIN[i]);
  bool trig[3];
  for (uint8_t k = 0; k < 3; k++) trig[k] = shtTrigger(k);
  delay(20);
  for (uint8_t k = 0; k < 3; k++) {
    float t, rh;
    if (trig[k] && shtFetch(k, t, rh)) { curT[3 + k] = t; curRH[3 + k] = rh; }
    else curT[3 + k] = curRH[3 + k] = NAN;
  }
  if (calOn) {
    for (uint8_t i = 0; i < 6; i++) {
      if (isnan(curT[i])) continue;
      sumT[i] += curT[i];
      if (i >= 3) sumRH[i] += curRH[i];
      n[i]++;
    }
  }
}

void printLive() {
  for (uint8_t i = 0; i < 6; i++) {
    Serial.print(NAME[i]); Serial.print(' ');
    if (isnan(curT[i])) Serial.print(F("--"));
    else {
      Serial.print(curT[i], 2);
      if (i >= 3) { Serial.print('/'); Serial.print(curRH[i], 1); Serial.print('%'); }
    }
    Serial.print(i == 2 ? F("  |  ") : F("  "));
  }
  if (calOn) { Serial.print(F("  cal ")); Serial.print((millis() - calStart) / 1000); Serial.print('s'); }
  Serial.println();
}

float med3(float a, float b, float c) { return max(min(a, b), min(max(a, b), c)); }

void startCal() {
  for (uint8_t i = 0; i < 6; i++) { sumT[i] = sumRH[i] = 0; n[i] = 0; }
  calOn = true;
  calStart = millis();
  benchLed(0, 0, 60);
  Serial.print(F("--- calibration: averaging all six for ")); Serial.print(CAL_S);
  Serial.println(F(" s, keep away from the sensors (any button aborts) ---"));
}

void printDefine(const __FlashStringHelper *name, float v) {
  Serial.print(F("#define ")); Serial.print(name); Serial.print(' ');
  Serial.print(v, 2); Serial.println('f');
}

void finishCal() {
  calOn = false;
  benchLed(0, 0, 0);
  float avT[6], avRH[6];
  Serial.println(F("--- result: raw averages ---"));
  for (uint8_t i = 0; i < 6; i++) {
    avT[i] = n[i] ? sumT[i] / n[i] : NAN;
    avRH[i] = (n[i] && i >= 3) ? sumRH[i] / n[i] : NAN;
    Serial.print(NAME[i]); Serial.print(F(": "));
    if (n[i] < 30) { Serial.println(F("too few reads, left out")); avT[i] = NAN; continue; }
    Serial.print(avT[i], 3); Serial.print(F(" C"));
    if (i >= 3) { Serial.print(' '); Serial.print(avRH[i], 2); Serial.print(F(" %")); }
    Serial.print(F("  (")); Serial.print(n[i]); Serial.println(F(" reads)"));
  }
  // Reference: median of the valid SHTs (mean of two, or the one)
  float vt[3], vrh[3];
  uint8_t m = 0;
  for (uint8_t k = 3; k < 6; k++) if (!isnan(avT[k])) { vt[m] = avT[k]; vrh[m] = avRH[k]; m++; }
  if (m == 0) { Serial.println(F("no SHT valid: no reference")); benchBlink(255, 0, 0, 3); return; }
  float refT = m == 3 ? med3(vt[0], vt[1], vt[2]) : m == 2 ? (vt[0] + vt[1]) / 2 : vt[0];
  float refRH = m == 3 ? med3(vrh[0], vrh[1], vrh[2]) : m == 2 ? (vrh[0] + vrh[1]) / 2 : vrh[0];
  Serial.print(F("reference (median of the SHTs): ")); Serial.print(refT, 3); Serial.print(F(" C "));
  Serial.print(refRH, 2); Serial.println(F(" %"));
  Serial.println(F("// config.h (paste to Claude):"));
  if (!isnan(avT[0])) printDefine(F("NTC_OFFSET_T1"), refT - avT[0]);
  if (!isnan(avT[1])) printDefine(F("NTC_OFFSET_T2"), refT - avT[1]);
  if (!isnan(avT[2])) printDefine(F("NTC_OFFSET_T3"), refT - avT[2]);
  if (!isnan(avT[3])) { printDefine(F("SHT_T_OFFSET_A"), refT - avT[3]); printDefine(F("SHT_RH_OFFSET_A"), refRH - avRH[3]); }
  if (!isnan(avT[4])) { printDefine(F("SHT_T_OFFSET_B"), refT - avT[4]); printDefine(F("SHT_RH_OFFSET_B"), refRH - avRH[4]); }
  if (!isnan(avT[5])) { printDefine(F("SHT_T_OFFSET_R"), refT - avT[5]); printDefine(F("SHT_RH_OFFSET_R"), refRH - avRH[5]); }
  Serial.println(F("// one-point calibration in air: corrects offset only, not Beta (use 01_ntc for that)"));
  benchBlink(0, 255, 0, 3);
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(50000);
  Wire.setWireTimeout(25000, true);
  bus2.begin();
  benchBanner(F("15_cal_all"));
  Serial.println(F("BTN1 long: 10 min calibration | BTN2 short: live line | serial c start, a abort"));
  Serial.println(F("live: T1 T2 T3 (NTC raw C) | A B R (SHT raw C/%RH)"));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  bool abortReq = false, startReq = false;
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c') startReq = true;
    if (c == 'a') abortReq = true;
  }
  if (calOn && (btn1.pressed || btn2.pressed || abortReq)) {
    calOn = false;
    btn1.consume();
    btn2.consume();
    benchLed(0, 0, 0);
    Serial.println(F("--- calibration aborted ---"));
  }
  if (!calOn && (btn1.longPress || startReq)) startCal();
  if (btn2.shortPress) printLive();

  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    readAll();
    printLive();
    if (calOn && millis() - calStart >= CAL_S * 1000UL) finishCal();
  }
}
