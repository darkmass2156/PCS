#pragma once
// Drawing toolkit for the dashboard screens (display rev 3). Canonical in firmware/controller/;
// tests/11_ui_dev holds a copy.
#include <Adafruit_ST7789.h>
#include "fonts.h"

// ROG palette (RGB565) + black
#define C_BLK 0x0000
#define C_WHT 0xF79E   // F3F3F3
#define C_LG  0xB596   // B2B2B2
#define C_DG  0x528A   // 525152
#define C_MAR 0x4020   // 420704
#define C_RED 0xA0E3   // A71C1E
#define C_HOT 0xD925   // DE272C

extern Adafruit_ST7789 *gfx;

// Built-in 6x8 font. Pads/truncates s to `width` chars ('l', 'r' or 'c' aligned) and draws it with a
// background colour, so a shorter value overwrites a longer one without clearing first.
void txt(int16_t x, int16_t y, uint8_t size, uint16_t fg, uint16_t bg, const char *s, uint8_t width, char align = 'l');

// Anti-aliased proportional text (fonts.h). Draws a fieldW x f.h box at (x, y): the text aligned
// ('l', 'r', 'c') inside, the rest background, streamed through one address window (no flicker,
// no clearing). The box grows to the text width if the text is wider. Degree sign = '\xF8'.
int16_t ftWidth(const Font &f, const char *s);
void ftxt(const Font &f, int16_t x, int16_t y, uint16_t fg, uint16_t bg, const char *s, int16_t fieldW, char align = 'l');

// Parallelogram: h rows of width w; row r is shifted right by slope * (h - 1 - r) (top leans right).
void fillSlant(int16_t x, int16_t y, int16_t w, int16_t h, float slope, uint16_t c);

// Seven-segment numbers. Characters: 0-9, '-', ' ' (blank cell) and '.' (narrow dot cell).
struct SegStyle {
  uint8_t w, h, t, gap;
  float slant;   // 0 = upright
};
int16_t segWidth(const SegStyle &st, const char *s);
// Draws s at (x, y). prev holds what is on screen (updated here); only segments that change are drawn.
// An empty prev means the cell area is already background.
void segText(int16_t x, int16_t y, const SegStyle &st, const char *s, char *prev, uint16_t fg, uint16_t bg);

// "12.3" style with one decimal, or "--" when invalid. Returns out.
char *fmt1(char *out, float v, bool ok);
