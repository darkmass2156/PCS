#include "sensors.h"
#include "config.h"
#include "softi2c.h"
#include "since.h"
#include <Wire.h>
#include <math.h>

NtcReading ntcT1, ntcT2, ntcT3;
ShtReading sht[SHT_COUNT];

static SoftI2C bus2(PIN_SDA2, PIN_SCL2);

// ---------------- NTC ----------------
struct NtcChan {
  uint8_t pin;
  float offset;
  NtcReading *out;
  float adcF;
  bool primed;
  Since bad;
};

static NtcChan ntc[3] = {
  {PIN_T1, NTC_OFFSET_T1, &ntcT1, 0, false, {false, 0}},
  {PIN_T2, NTC_OFFSET_T2, &ntcT2, 0, false, {false, 0}},
  {PIN_T3, NTC_OFFSET_T3, &ntcT3, 0, false, {false, 0}},
};
static uint32_t ntcAt;

// Serial SIM overrides (bench only): 0..2 = T1..T3, 3 = T_room. NAN = live value.
static float ovr[4] = {NAN, NAN, NAN, NAN};

static float ntcToC(float adc) {
  if (adc < NTC_ADC_SHORT || adc > NTC_ADC_OPEN) return NAN;   // short / open
  float r = NTC_R_PULLUP * adc / (1023.0f - adc);
#if NTC_TRIMPOT_SIM
  if (r < SIM_SHORT_OHM || r > 10500.0f) return NAN;
  return SIM_T_MIN_C + (SIM_T_MAX_C - SIM_T_MIN_C) * r / 10000.0f;
#else
  float invT = 1.0f / 298.15f + log(r / NTC_R25) / NTC_BETA;
  return 1.0f / invT - 273.15f;
#endif
}

static void ntcUpdate(uint32_t now) {
  if (now - ntcAt < 100) return;
  ntcAt = now;
  for (uint8_t k = 0; k < 3; k++) {
    NtcChan &ch = ntc[k];
    analogRead(ch.pin);                 // settle the mux
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 4; i++) sum += analogRead(ch.pin);
    float adc = sum / 4.0f;
    // Open-pin check. A pin cut off from its node (broken ribbon wire, loose housing) floats, and the ADC then
    // returns what the previous channel left on its sample cap: a plausible, wrong temperature. With the internal
    // pull-up (~35k) on for ~100 us a floating pin rises to ~1023, while the real node (10k pull-up + 100 nF on LG)
    // moves only a few counts. The 100 ms until the next read lets the node settle again.
    pinMode(ch.pin, INPUT_PULLUP);
    delayMicroseconds(100);
    uint16_t probe = analogRead(ch.pin);
    pinMode(ch.pin, INPUT);
    bool floating = probe > NTC_ADC_OPEN && probe > adc + NTC_FLOAT_DELTA;
    bool openShort = floating || adc < NTC_ADC_SHORT || adc > NTC_ADC_OPEN;   // raw reading, not the filter
    if (!ch.primed || openShort) { ch.adcF = adc; ch.primed = !openShort; }   // restart the filter on recovery
    ch.adcF += (adc - ch.adcF) / 8.0f;  // ~0.8 s time constant at 10 Hz
    float c = openShort ? NAN : ntcToC(ch.adcF);
    if (!isnan(c) && !NTC_TRIMPOT_SIM) c += ch.offset;
    if (!isnan(ovr[k])) { c = ovr[k]; floating = false; }   // serial SIM override
    NtcReading &o = *ch.out;
    o.c = c;
    o.adc = ch.adcF;
    o.floating = floating;
    o.inRange = !isnan(c) && c >= NTC_MIN_C && c <= NTC_MAX_C;
    if (o.inRange) ch.bad.clear();
    else ch.bad.hold(now);
    o.valid = o.inRange || !ch.bad.elapsed(now, NTC_FAULT_MS);
  }
}

// ---------------- SHT3x ----------------
static const uint8_t shtAddr[SHT_COUNT] = {0x44, 0x45, 0x44};
static const uint8_t CMD_SINGLE_HIGH_NOSTRETCH[2] = {0x24, 0x00};
static uint32_t shtTrigAt;
static bool shtPending;
static bool shtTrigOk[SHT_COUNT];

static uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

uint16_t i2cRecoveries;

static void wireStart() {
  Wire.begin();
  Wire.setClock(50000);                          // ~1.5 m run to the case
  Wire.setWireTimeout(SHT_WIRE_TIMEOUT_US, true);   // a stuck bus can't hang the loop
}

// After a Wire timeout: release the TWI, clock SCL 9 times so a slave holding SDA low finishes its byte,
// send a STOP, then restart Wire. The 4.7k pull-ups on LG pull the lines high (INPUT, not OUTPUT HIGH).
static void wireRecover() {
  Wire.end();
  pinMode(SDA, INPUT);
  for (uint8_t k = 0; k < 9 && digitalRead(SDA) == LOW; k++) {
    pinMode(SCL, OUTPUT);   // PORT bit is 0 after Wire.end(): OUTPUT = LOW
    delayMicroseconds(10);
    pinMode(SCL, INPUT);
    delayMicroseconds(10);
  }
  pinMode(SDA, OUTPUT);     // STOP: SDA low -> high while SCL is high
  delayMicroseconds(10);
  pinMode(SDA, INPUT);
  delayMicroseconds(10);
  wireStart();
  Wire.clearWireTimeoutFlag();
  i2cRecoveries++;
}

static bool wireTimedOut() {
  if (!Wire.getWireTimeoutFlag()) return false;
  wireRecover();
  return true;
}

static bool busWrite(uint8_t i, const uint8_t *d, uint8_t n) {
  if (i == SHT_R) return bus2.write(shtAddr[i], d, n);
  Wire.beginTransmission(shtAddr[i]);
  Wire.write(d, n);
  bool ok = Wire.endTransmission() == 0;
  return !wireTimedOut() && ok;
}

static bool busRead(uint8_t i, uint8_t *d, uint8_t n) {
  if (i == SHT_R) return bus2.read(shtAddr[i], d, n);
  bool ok = Wire.requestFrom(shtAddr[i], n) == n;
  if (wireTimedOut() || !ok) return false;
  for (uint8_t k = 0; k < n; k++) d[k] = Wire.read();
  return true;
}

float dewPointMagnus(float t, float rh) {
  const float a = 17.62f, b = 243.12f;
  if (rh < 0.1f) rh = 0.1f;
  float g = log(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

static void shtResult(uint8_t i, bool ok, const uint8_t *d) {
  ShtReading &s = sht[i];
  if (ok) ok = crc8(d, 2) == d[2] && crc8(d + 3, 2) == d[5];
  if (ok) {
    static const float T_OFS[SHT_COUNT] = {SHT_T_OFFSET_A, SHT_T_OFFSET_B, SHT_T_OFFSET_R};
    float t = -45.0f + 175.0f * (uint16_t)((d[0] << 8) | d[1]) / 65535.0f + T_OFS[i];
    static const float RH_OFS[SHT_COUNT] = {SHT_RH_OFFSET_A, SHT_RH_OFFSET_B, SHT_RH_OFFSET_R};
    float rh = constrain(100.0f * (uint16_t)((d[3] << 8) | d[4]) / 65535.0f + RH_OFS[i], 0.0f, 100.0f);
    ok = t > -20.0f && t < 70.0f && rh > 0.5f && rh <= 100.0f;
    if (ok) {
      s.t = t;
      s.rh = rh;
      s.dp = dewPointMagnus(t, rh);
    }
  }
  if (ok) {
    s.fails = 0;
    if (s.goods < SHT_GOOD_LIMIT) s.goods++;
    if (s.goods >= SHT_GOOD_LIMIT) s.valid = true;
  } else {
    s.goods = 0;
    s.errCount++;
    if (s.fails < SHT_FAIL_LIMIT) s.fails++;
    if (s.fails >= SHT_FAIL_LIMIT) s.valid = false;
  }
}

static void shtUpdate(uint32_t now) {
  if (!shtPending) {
    if (now - shtTrigAt < SHT_PERIOD_MS) return;
    shtTrigAt = now;
    for (uint8_t i = 0; i < SHT_COUNT; i++) shtTrigOk[i] = busWrite(i, CMD_SINGLE_HIGH_NOSTRETCH, 2);
    shtPending = true;
    return;
  }
  if (now - shtTrigAt < 20) return;   // high-repeatability conversion ≤ 15.5 ms
  shtPending = false;
  for (uint8_t i = 0; i < SHT_COUNT; i++) {
    uint8_t d[6];
    bool ok = shtTrigOk[i] && busRead(i, d, 6);
    shtResult(i, ok, d);
    if (i == SHT_R && !isnan(ovr[3]) && sht[SHT_R].valid) sht[SHT_R].t = ovr[3];   // SIM: T only, DP stays real
  }
}

// '?' diagnostics for the room SHT30: idle levels with only the external pull-ups, then an ACK probe on
// 0x44 / 0x45 with the pins as wired and swapped. Blocking for a few ms, called on '?' only.
void sensorsRoomDiag() {
  pinMode(PIN_SDA2, INPUT);
  pinMode(PIN_SCL2, INPUT);
  delayMicroseconds(200);
  Serial.print(F("# ROOM SHT30 idle (ext pull-ups only): D")); Serial.print(PIN_SDA2);
  Serial.print(digitalRead(PIN_SDA2) ? F(" HIGH  D") : F(" LOW  D")); Serial.print(PIN_SCL2);
  Serial.println(digitalRead(PIN_SCL2) ? F(" HIGH") : F(" LOW"));
  static const uint8_t CMD_STATUS[2] = {0xF3, 0x2D};
  for (uint8_t swap = 0; swap < 2; swap++) {
    uint8_t sda = swap ? PIN_SCL2 : PIN_SDA2, scl = swap ? PIN_SDA2 : PIN_SCL2;
    SoftI2C b(sda, scl);
    b.begin();
    for (uint8_t a = 0x44; a <= 0x45; a++) {
      Serial.print(F("#   SDA D")); Serial.print(sda); Serial.print(F(" SCL D")); Serial.print(scl);
      Serial.print(F(" 0x")); Serial.print(a, HEX);
      if (!b.write(a, CMD_STATUS, 2)) { Serial.println(F(" no ACK")); continue; }
      delay(2);
      uint8_t d[3];
      bool ok = b.read(a, d, 3);
      Serial.print(F(" ACK"));
      if (ok) {
        Serial.print(F(", status 0x")); Serial.print((d[0] << 8) | d[1], HEX);
        Serial.print(crc8(d, 2) == d[2] ? F(" crc ok") : F(" CRC BAD"));
      }
      Serial.println();
    }
  }
  bus2.begin();
  Serial.print(F("#   room reading: ")); Serial.print(sht[SHT_R].t, 2); Serial.print(F(" C "));
  Serial.print(sht[SHT_R].rh, 1); Serial.print(F(" %  errs ")); Serial.print(sht[SHT_R].errCount);
  Serial.println(sht[SHT_R].valid ? F(" valid") : F(" INVALID"));
}

float dewPointMax(uint8_t &validCount, float &spread) {
  float mx = NAN, mn = NAN;
  validCount = 0;
  for (uint8_t i = 0; i < SHT_COUNT; i++) {
    if (!sht[i].valid) continue;
    float dp = sht[i].dp;
    if (validCount == 0 || dp > mx) mx = dp;
    if (validCount == 0 || dp < mn) mn = dp;
    validCount++;
  }
  spread = validCount >= 2 ? mx - mn : 0.0f;
  return mx;
}

void sensorsInit() {
  wireStart();
  bus2.begin();
  for (ShtReading &s : sht) {
    s.t = s.rh = s.dp = NAN;
    s.valid = false;
  }
  for (NtcChan &ch : ntc) ch.out->c = NAN;
  shtTrigAt = millis() - SHT_PERIOD_MS;
}

void sensorsUpdate(uint32_t now) {
  ntcUpdate(now);
  shtUpdate(now);
}

void sensorsSetOverride(uint8_t idx, float v) { if (idx < 4) ovr[idx] = v; }
float sensorsOverride(uint8_t idx) { return idx < 4 ? ovr[idx] : NAN; }
bool sensorsOverrideActive() {
  for (float v : ovr) if (!isnan(v)) return true;
  return false;
}
