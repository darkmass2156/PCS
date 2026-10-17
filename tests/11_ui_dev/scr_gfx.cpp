#include "scr_gfx.h"
#include <math.h>

Adafruit_ST7789 *gfx;

void txt(int16_t x, int16_t y, uint8_t size, uint16_t fg, uint16_t bg, const char *s, uint8_t width, char align) {
  char b[48];
  if (width > sizeof(b) - 1) width = sizeof(b) - 1;
  uint8_t n = strlen(s);
  if (n > width) n = width;
  uint8_t pad = width - n;
  uint8_t left = align == 'r' ? pad : align == 'c' ? pad / 2 : 0;
  memset(b, ' ', width);
  memcpy(b + left, s, n);
  b[width] = 0;
  gfx->setTextSize(size);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x, y);
  gfx->print(b);
}

void fillSlant(int16_t x, int16_t y, int16_t w, int16_t h, float slope, uint16_t c) {
  if (slope == 0) {
    gfx->fillRect(x, y, w, h, c);
    return;
  }
  for (int16_t r = 0; r < h; r++) gfx->drawFastHLine(x + (int16_t)lround(slope * (h - 1 - r)), y + r, w, c);
}

// ---------------- seven-segment ----------------
// bit 0..6 = a b c d e f g
static const uint8_t DIGITS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

static uint8_t segMask(char c) {
  if (c >= '0' && c <= '9') return DIGITS[c - '0'];
  if (c == '-') return 0x40;
  return 0;
}

static int16_t cellW(const SegStyle &st, char c) { return c == '.' ? st.t : st.w; }

int16_t segWidth(const SegStyle &st, const char *s) {
  int16_t w = 0;
  for (const char *p = s; *p; p++) w += cellW(st, *p) + st.gap;
  return w ? w - st.gap : 0;
}

// One segment of the cell at (cx, cy); the slant follows the cell, so the whole digit leans.
static void segDraw(int16_t cx, int16_t cy, const SegStyle &st, uint8_t seg, uint16_t c) {
  const int16_t w = st.w, h = st.h, t = st.t, hm = h / 2;
  int16_t x0, y0, x1, y1;
  switch (seg) {
    case 0: x0 = t;     y0 = 0;          x1 = w - t; y1 = t;              break;  // a
    case 1: x0 = w - t; y0 = t;          x1 = w;     y1 = hm;             break;  // b
    case 2: x0 = w - t; y0 = hm;         x1 = w;     y1 = h - t;          break;  // c
    case 3: x0 = t;     y0 = h - t;      x1 = w - t; y1 = h;              break;  // d
    case 4: x0 = 0;     y0 = hm;         x1 = t;     y1 = h - t;          break;  // e
    case 5: x0 = 0;     y0 = t;          x1 = t;     y1 = hm;             break;  // f
    default: x0 = t;    y0 = hm - t / 2; x1 = w - t; y1 = hm - t / 2 + t; break;  // g
  }
  if (st.slant == 0) {
    gfx->fillRect(cx + x0, cy + y0, x1 - x0, y1 - y0, c);
    return;
  }
  for (int16_t r = y0; r < y1; r++)
    gfx->drawFastHLine(cx + x0 + (int16_t)lround(st.slant * (h - 1 - r)), cy + r, x1 - x0, c);
}

static void dotDraw(int16_t cx, int16_t cy, const SegStyle &st, uint16_t c) {
  int16_t off = (int16_t)lround(st.slant * (st.t - 1));
  gfx->fillRect(cx + off, cy + st.h - st.t, st.t, st.t, c);
}

void segText(int16_t x, int16_t y, const SegStyle &st, const char *s, char *prev, uint16_t fg, uint16_t bg) {
  // Same cell pattern (length and dot position) -> diff segment by segment, else clear and redraw.
  bool same = strlen(prev) == strlen(s);
  for (uint8_t i = 0; same && s[i]; i++)
    if ((s[i] == '.') != (prev[i] == '.')) same = false;
  if (!same && prev[0]) {
    int16_t wOld = segWidth(st, prev) + (int16_t)lround(st.slant * st.h) + 1;
    gfx->fillRect(x, y, wOld, st.h, bg);
    prev[0] = 0;
    same = false;
  }
  int16_t cx = x;
  for (uint8_t i = 0; s[i]; i++) {
    char c = s[i], o = same ? prev[i] : ' ';
    if (c == '.') {
      if (!same) dotDraw(cx, y, st, fg);
    } else {
      uint8_t mn = segMask(c), mo = segMask(o);
      for (uint8_t k = 0; k < 7; k++) {
        uint8_t bit = 1 << k;
        if ((mn & bit) && !(mo & bit)) segDraw(cx, y, st, k, fg);
        else if (!(mn & bit) && (mo & bit)) segDraw(cx, y, st, k, bg);
      }
    }
    cx += cellW(st, c) + st.gap;
  }
  strcpy(prev, s);
}

char *fmt1(char *out, float v, bool ok) {
  if (!ok || isnan(v)) strcpy(out, "--");
  else dtostrf(v, 0, 1, out);
  return out;
}
