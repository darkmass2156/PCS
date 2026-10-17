#pragma once
// Boot sequence, ~6 s, no text except the version under the ROG logo:
//   1. Pingu: one of the penguins in boot_sprite.h (random per boot) goes "noot noot" twice.
//   2. Glitch, both ways at once: the penguin tears sideways while torn bands of the ROG logo (rog_logo.h)
//      cut in; the logo bands take over, their displacement shrinks and the logo locks in. Scanline noise
//      and two inverted-colour flashes on top.
//   3. ROG: the logo resolves clean in white, a red scan passes over it, red underline + firmware version,
//      hold, clear to black for the dashboard.
// Non-blocking: update() does one small drawing step per call, so loop(), the LED heartbeat and
// the watchdog keep running. Everything streams through address windows (no per-pixel commands).
// Canonical in firmware/controller/; tests/11_ui_dev has a copy.
// Sprites: tools/sprites/make_pingu.py -> boot_sprite.h, tools/sprites/make_rog.py -> rog_logo.h.
#include <Adafruit_ST7789.h>
#include "boot_sprite.h"
#include "rog_logo.h"
#include "scr_gfx.h"   // palette

class BootSplash {
 public:
  // invertNormal: the panel's normal invert setting (the glitch flashes the opposite briefly).
  void begin(Adafruit_ST7789 *t, uint16_t w, uint16_t h, uint8_t which, const char *version, bool invertNormal,
             uint32_t now) {
    tft = t;
    W = w;
    H = h;
    ver = version;
    invNormal = invertNormal;
    sp = &SPLASH_SPRITES[which % SPLASH_COUNT];
    S = 4;
    while (S > 1 && (sp->w * S > W - 8 || sp->h * S > H - 8)) S--;
    sx = (W - sp->w * S) / 2;
    sy = (H - sp->h * S) / 2;
    lx = (W - 280) / 2 + ROG_X0;
    ly = (H - 240) / 2 + ROG_Y0;
    step = BG;
    strip = 0;
    at = now;
  }

  bool active() const { return step != DONE; }

  // Returns true once the whole sequence has finished and the screen is black again.
  bool update(uint32_t now) {
    switch (step) {
      case BG:        // sprite background in strips (a full fill is ~0.5 s at 2 MHz)
        if (fillStrip(sp->bg)) {
          step = SPRITE;
          idx = ry = 0;
        }
        break;
      case SPRITE:    // 6 sprite rows per call
        if (blit(sp->base, sx, sy, sp->w, sp->h, 6)) next(ANIM, now);
        break;
      case ANIM:
        anim(now);
        break;
      case GLITCH:
        glitch(now);
        break;
      case SETTLE:    // clear the margins around the logo box, then the logo clean in white (20 rows/call)
        if (strip == 0) {
          tft->fillRect(0, 0, W, ly, 0x0000);
          tft->fillRect(0, ly + ROG_H, W, H - ly - ROG_H, 0x0000);
          tft->fillRect(0, ly, lx, ROG_H, 0x0000);
          tft->fillRect(lx + ROG_W, ly, W - lx - ROG_W, ROG_H, 0x0000);
        }
        rog(C_WHT, 0, ROG_W, strip, 20, 0);
        strip += 20;
        if (strip >= ROG_H) {
          strip = 0;
          next(SWEEP, now);
        }
        break;
      case SWEEP:     // a red scan, 30 px wide, crosses the white logo; stage = slice number
        if (now - at < 35) break;
        at = now;
        {
          uint16_t a = stage * 30;
          if (stage) rog(C_WHT, a - 30, a < ROG_W ? a : ROG_W, 0, ROG_H, 0);   // previous slice back to white
          if (a < ROG_W) {
            rog(C_HOT, a, a + 30 < ROG_W ? a + 30 : ROG_W, 0, ROG_H, 0);
            stage++;
          } else {
            tft->fillRect(W / 2 - 80, ly + ROG_H + 18, 160, 2, C_HOT);
            tft->setTextSize(1);
            tft->setTextColor(C_LG, 0x0000);
            tft->setCursor(W / 2 - 3 * (8 + strlen(ver)), ly + ROG_H + 24);
            tft->print(F("PCS  fw "));
            tft->print(ver);
            next(ROG_HOLD, now);
          }
        }
        break;
      case ROG_HOLD:
        if (now - at >= 1100) {
          strip = 0;
          step = CLEAR;
        }
        break;
      case CLEAR:     // back to black for the dashboard
        if (fillStrip(0x0000)) step = DONE;
        break;
      case DONE:
        break;
    }
    return step == DONE;
  }

 private:
  enum Step : uint8_t { BG, SPRITE, ANIM, GLITCH, SETTLE, SWEEP, ROG_HOLD, CLEAR, DONE };
  Adafruit_ST7789 *tft;
  const SplashSprite *sp;
  const char *ver;
  bool invNormal, inverted;
  uint16_t W, H, strip;
  uint8_t S;
  int16_t sx, sy, lx, ly;
  Step step = DONE;
  uint16_t idx;
  uint8_t ry, stage;
  uint32_t at;

  void next(Step s, uint32_t now) {
    step = s;
    stage = 0;
    at = now;
  }

  bool fillStrip(uint16_t c) {
    uint16_t h = strip + 24 > H ? H - strip : 24;
    tft->fillRect(0, strip, W, h, c);
    strip += 24;
    if (strip < H) return false;
    strip = 0;
    return true;
  }

  uint16_t color(uint8_t c) const { return c ? pgm_read_word(sp->pal + c) : sp->bg; }

  // Streams up to `rows` sprite rows (from where the last call stopped) through ONE address window.
  // Each sprite row is replayed S times for the vertical scale. True when all h rows are done.
  bool blit(const uint8_t *data, int16_t ox, int16_t oy, uint8_t w, uint8_t h, uint8_t rows) {
    if (ry + rows > h) rows = h - ry;
    tft->startWrite();
    tft->setAddrWindow(ox, oy + ry * S, w * S, rows * S);
    for (uint8_t r = 0; r < rows; r++, ry++) {
      uint16_t rowStart = idx;
      for (uint8_t k = 0; k < S; k++) {
        idx = rowStart;
        for (uint8_t x = 0; x < w;) {
          uint8_t b = pgm_read_byte(data + idx++);
          uint8_t n = (b & 0x0F) + 1;
          tft->writeColor(color(b >> 4), (uint32_t)n * S);
          x += n;
        }
      }
    }
    tft->endWrite();
    return ry >= h;
  }

  // Byte offset of sprite row `row` in the base image.
  uint16_t rowOffset(uint8_t row) const {
    uint16_t i = 0;
    for (uint8_t r = 0; r < row; r++)
      for (uint8_t x = 0; x < sp->w;) x += (pgm_read_byte(sp->base + i++) & 0x0F) + 1;
    return i;
  }

  void beak(bool noot) {
    idx = ry = 0;
    blit(noot ? sp->noot : sp->beak, sx + sp->px * S, sy + sp->py * S, sp->pw, sp->ph, sp->ph);
  }

  // Timeline (ms after the sprite is up): noot 600, back 1000, noot 1250, back 1650, glitch 2300.
  void anim(uint32_t now) {
    static const uint16_t T[] = {600, 1000, 1250, 1650, 2300};
    if (stage >= 5 || now - at < T[stage]) return;
    if (stage == 4) {
      next(GLITCH, now);
      inverted = false;
      return;
    }
    beak(stage == 0 || stage == 2);
    stage++;
  }

  // Penguin out, logo in, overlapping. One frame every 45 ms, 20 frames.
  void tear() {   // a band of sprite rows redrawn shifted sideways
    uint8_t r0 = random(sp->h), n = 2 + random(6);
    if (r0 + n > sp->h) n = sp->h - r0;
    int16_t dx = random(-24, 25);
    if (sx + dx < 0) dx = -sx;
    if (sx + dx + sp->w * S > W) dx = W - sx - sp->w * S;
    tft->fillRect(0, sy + r0 * S, W, n * S, sp->bg);
    idx = rowOffset(r0);
    ry = r0;
    blit(sp->base, sx + dx, sy, sp->w, r0 + n, n);
  }

  void logoBand(int16_t amp) {   // a band of logo rows on black, displaced by up to +-amp
    uint16_t r0 = random(ROG_H), n = 3 + random(10);
    if (r0 + n > ROG_H) n = ROG_H - r0;
    tft->fillRect(0, ly + r0, W, n, 0x0000);
    rog(random(5) ? C_WHT : C_HOT, 0, ROG_W, r0, n, amp ? random(-amp, amp + 1) : 0);
  }

  void glitch(uint32_t now) {
    if (now - at < 45) return;
    at = now;
    uint8_t f = stage++;
    if (inverted) {   // a flash lasts one frame
      tft->invertDisplay(invNormal);
      inverted = false;
    }
    if (f >= 20) {
      tft->invertDisplay(invNormal);
      strip = 0;
      step = SETTLE;
      return;
    }
    if (f < 12) tear();
    if (f < 5) tear();
    if (f >= 5) {
      uint8_t bands = f < 12 ? 1 + (f - 5) / 3 : 6;
      int16_t amp = f < 12 ? 40 : (20 - f) * 5;   // displacement shrinks as the logo locks in
      for (uint8_t k = 0; k < bands; k++) logoBand(amp);
    }
    if (f >= 12) {   // wipe what is left of the penguin above and below the logo rows
      for (uint8_t k = 0; k < 2; k++) {
        bool above = random(2);
        int16_t span = above ? ly : H - ly - ROG_H, h = 8 + random(14);
        if (span <= 0) continue;
        int16_t y = (above ? 0 : ly + ROG_H) + random(span);
        if (h > (above ? ly : H) - y) h = (above ? ly : H) - y;
        tft->fillRect(0, y, W, h, 0x0000);
      }
    }
    static const uint16_t NOISE[4] = {C_HOT, C_WHT, C_DG, C_MAR};
    for (uint8_t k = 0; k < (f < 16 ? 3 : 1); k++) {   // scanline noise
      int16_t y = random(H), x = random(W / 2), w = 20 + random(W);
      if (x + w > W) w = W - x;
      tft->fillRect(x, y, w, 1 + random(3), NOISE[random(4)]);
    }
    if (f == 8 || f == 15) {
      tft->invertDisplay(!invNormal);
      inverted = true;
    }
  }

  // Streams logo rows [row0, row0 + rows) restricted to columns [a, b), shifted dx px sideways and clipped
  // to the screen: logo pixels in fg, the rest black.
  void rog(uint16_t fg, uint16_t a, uint16_t b, uint16_t row0, uint16_t rows, int16_t dx) {
    if (row0 + rows > ROG_H) rows = ROG_H - row0;
    if (b > ROG_W) b = ROG_W;
    int16_t left = lx + dx;
    if (left + (int16_t)a < 0) a = -left;
    if (left + (int16_t)b > (int16_t)W) b = W - left;
    if ((int16_t)b <= (int16_t)a || !rows) return;
    uint16_t i = 0;
    for (uint16_t r = 0; r < row0; r++)            // skip to row0
      for (uint16_t x = 0; x < ROG_W;) x += pgm_read_byte(ROG_RLE + i++);
    tft->startWrite();
    tft->setAddrWindow(left + a, ly + row0, b - a, rows);
    for (uint16_t r = 0; r < rows; r++) {
      bool on = false;
      for (uint16_t x = 0; x < ROG_W;) {
        uint16_t n = pgm_read_byte(ROG_RLE + i++);
        uint16_t s = x > a ? x : a, e = x + n < b ? x + n : b;   // overlap with [a, b)
        if (e > s) tft->writeColor(on ? fg : 0x0000, e - s);
        x += n;
        on = !on;
      }
    }
    tft->endWrite();
  }
};
