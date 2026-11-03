#pragma once
#include <Arduino.h>

// Debounced tach sampling for 05_fan_pwm_tach (kept in a header: the Arduino IDE puts auto-generated prototypes
// above struct definitions in the .ino, which breaks functions taking these types).
struct Deb { bool high; uint8_t n; };
static Deb d1 = {true, 0}, d2 = {true, 0};
// true on a debounced HIGH -> LOW change
static inline bool debounce(Deb &d, bool high) {
  if (high == d.high) { d.n = 0; return false; }
  if (++d.n < TACH_DEBOUNCE) return false;
  d.high = high;
  d.n = 0;
  return !high;
}
static inline void counted(uint32_t t, volatile uint32_t &last, volatile uint32_t &mn, volatile uint32_t &mx,
                    volatile uint16_t &f) {
  uint32_t iv = t - last;
  if (iv < mn) mn = iv;
  if (iv > mx) mx = iv;
  last = t;
  f++;
}
