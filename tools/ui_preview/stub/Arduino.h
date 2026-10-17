#pragma once
// Host stand-in for the bits of the Arduino core the display code uses (tools/ui_preview only).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define F(s) (s)
inline char *dtostrf(double v, signed char w, unsigned char p, char *out) { sprintf(out, "%*.*f", w, p, v); return out; }
inline long random(long n) { return n > 0 ? rand() % n : 0; }
inline long random(long a, long b) { return b > a ? a + rand() % (b - a) : a; }
