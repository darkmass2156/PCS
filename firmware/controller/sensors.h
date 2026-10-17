#pragma once
#include <Arduino.h>

enum { SHT_A = 0, SHT_B = 1, SHT_R = 2, SHT_COUNT = 3 };

struct NtcReading {
  float c;          // NAN when the ADC reads a short or open
  bool inRange;     // -40..80 right now
  bool valid;       // in range, or out of range for less than NTC_FAULT_MS
};

struct ShtReading {
  float t, rh, dp;
  bool valid;       // false until SHT_GOOD_LIMIT good reads; false after SHT_FAIL_LIMIT bad ones
  uint8_t fails, goods;
  uint16_t errCount;
};

extern NtcReading ntcT1, ntcT2, ntcT3;
extern ShtReading sht[SHT_COUNT];

void sensorsInit();
void sensorsUpdate(uint32_t now);   // non-blocking, call every loop pass

// Max of the valid dew points; returns NAN and count 0 if none.
float dewPointMax(uint8_t &validCount, float &spread);
float dewPointMagnus(float t, float rh);
