#pragma once
// Host framebuffer stand-in for Adafruit_ST7789 (tools/ui_preview only): same drawing calls, 280x240.
#include "Arduino.h"
#include <algorithm>
#include "font5x7.h"
#define ST77XX_BLACK 0x0000
class Adafruit_ST7789 {
 public:
  static const int FW = 280, FH = 240;
  uint16_t fb[FH][FW];
  bool inv = false;
  long windows = 0;
  void px(int x, int y, uint16_t c) { if (x >= 0 && x < FW && y >= 0 && y < FH) fb[y][x] = c; }
  void drawPixel(int x, int y, uint16_t c) { px(x, y, c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, c); }
  void fillScreen(uint16_t c) { fillRect(0, 0, FW, FH, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) { drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c); drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { px(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
  }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) { drawLine(x0, y0, x1, y1, c); drawLine(x1, y1, x2, y2, c); drawLine(x2, y2, x0, y0, c); }
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int miny = std::min(y0, std::min(y1, y2)), maxy = std::max(y0, std::max(y1, y2));
    for (int y = miny; y <= maxy; y++) {
      double xs[3]; int n = 0; int P[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
      for (int k = 0; k < 3; k++) { int *a = P[k], *b = P[(k + 1) % 3]; if ((a[1] <= y && b[1] >= y) || (b[1] <= y && a[1] >= y)) { if (a[1] == b[1]) { xs[n++] = a[0]; if (n < 3) xs[n++] = b[0]; } else xs[n++] = a[0] + (double)(y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]); } if (n >= 3) break; }
      if (!n) continue; double lo = xs[0], hi = xs[0]; for (int k = 1; k < n; k++) { lo = std::min(lo, xs[k]); hi = std::max(hi, xs[k]); }
      drawFastHLine((int)lround(lo), y, (int)lround(hi) - (int)lround(lo) + 1, c);
    }
  }
  void drawCircle(int x0, int y0, int r, uint16_t c) { for (int a = 0; a < 360; a++) px((int)lround(x0 + r * cos(a * M_PI / 180)), (int)lround(y0 + r * sin(a * M_PI / 180)), c); }
  void fillCircle(int x0, int y0, int r, uint16_t c) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= r * r + r) px(x0 + x, y0 + y, c); }
  // text (GFX classic-font semantics: 6x8 cell, scaled, background filled when bg != fg)
  int cx = 0, cy = 0, ts = 1; uint16_t tfg = 0xFFFF, tbg = 0xFFFF;
  void setTextSize(int s) { ts = s; }
  void setTextColor(uint16_t f) { tfg = tbg = f; }
  void setTextColor(uint16_t f, uint16_t b) { tfg = f; tbg = b; }
  void setCursor(int x, int y) { cx = x; cy = y; }
  void cp437(bool) {}
  void print(char ch) {
    unsigned char c = (unsigned char)ch;
    for (int i = 0; i < 6; i++) for (int j = 0; j < 8; j++) {
      bool on = i < 5 && (FONT5X7[c][i] >> j) & 1;
      if (on) fillRect(cx + i * ts, cy + j * ts, ts, ts, tfg);
      else if (tbg != tfg) fillRect(cx + i * ts, cy + j * ts, ts, ts, tbg);
    }
    cx += 6 * ts;
  }
  void print(const char *s) { while (*s) print(*s++); }
  // streamed writes
  int wx, wy, ww, wh; long pos, area;
  void startWrite() {}
  void endWrite() { if (pos != area) { fprintf(stderr, "window not filled %ld/%ld\n", pos, area); exit(1); } }
  void setAddrWindow(int x, int y, int w, int h) {
    if (x < 0 || y < 0 || x + w > FW || y + h > FH) { fprintf(stderr, "window off screen %d,%d %dx%d\n", x, y, w, h); exit(1); }
    wx = x; wy = y; ww = w; wh = h; pos = 0; area = (long)w * h; windows++;
  }
  void writeColor(uint16_t c, uint32_t n) { for (uint32_t i = 0; i < n; i++) { if (pos >= area) { fprintf(stderr, "window overflow\n"); exit(1); } px(wx + pos % ww, wy + pos / ww, c); pos++; } }
  void invertDisplay(bool i) { inv = i; }
  void savePPM(const char *path) {
    FILE *f = fopen(path, "wb"); fprintf(f, "P6 %d %d 255\n", FW, FH);
    for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) { uint16_t c = fb[y][x]; if (inv) c = ~c; fputc((c >> 11) << 3, f); fputc(((c >> 5) & 63) << 2, f); fputc((c & 31) << 3, f); }
    fclose(f);
  }
};
