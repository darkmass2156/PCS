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

static float ntcToC(float adc) {
  if (adc < 1.0f || adc > 1022.0f) return NAN;   // short / open
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
  for (NtcChan &ch : ntc) {
    analogRead(ch.pin);                 // settle the mux
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 4; i++) sum += analogRead(ch.pin);
    float adc = sum / 4.0f;
    if (!ch.primed) { ch.adcF = adc; ch.primed = true; }
    ch.adcF += (adc - ch.adcF) / 8.0f;  // ~0.8 s time constant at 10 Hz
    float c = ntcToC(ch.adcF);
    if (!isnan(c) && !NTC_TRIMPOT_SIM) c += ch.offset;
    NtcReading &o = *ch.out;
    o.c = c;
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

static bool busWrite(uint8_t i, const uint8_t *d, uint8_t n) {
  if (i == SHT_R) return bus2.write(shtAddr[i], d, n);
  Wire.beginTransmission(shtAddr[i]);
  Wire.write(d, n);
  return Wire.endTransmission() == 0;
}

static bool busRead(uint8_t i, uint8_t *d, uint8_t n) {
  if (i == SHT_R) return bus2.read(shtAddr[i], d, n);
  if (Wire.requestFrom(shtAddr[i], n) != n) return false;
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
    float t = -45.0f + 175.0f * (uint16_t)((d[0] << 8) | d[1]) / 65535.0f;
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
  }
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
  Wire.begin();
  Wire.setClock(50000);                // ~1.5 m run to the case
  Wire.setWireTimeout(5000, true);     // a stuck bus can't hang the loop
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
