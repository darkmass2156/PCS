#pragma once
// Boot sequence, ~6 s, no text except the version under the ROG logo:
//   1. Pingu: one of the penguins in boot_sprite.h (random per boot) goes "noot noot" twice.
//   2. Glitch: bands of the penguin tear sideways, scanline noise, two inverted-colour flashes,
//      then the picture collapses into black ("signal lost").
//   3. ROG: the logo (rog_logo.h) appears in dark grey, a white wipe with a red edge runs across it,
//      a red underline and the firmware version, hold, clear to black for the dashboard.
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
      case BLACK:
        if (fillStrip(0x0000)) {
          step = ROG_DIM;
          strip = 0;
        }
        break;
      case ROG_DIM:   // logo in dark grey, 20 rows per call
        rog(C_DG, 0, ROG_W, strip, 20);
        strip += 20;
        if (strip >= ROG_H) next(ROG_WAIT, now);
        break;
      case ROG_WAIT:
        if (now - at >= 250) {
          strip = 0;   // sweep position
          next(ROG_SWEEP, now);
        }
        break;
      case ROG_SWEEP:   // white wipe with a red leading edge, 10 steps
        if (now - at < 45) break;
        at = now;
        {
          uint16_t a = strip, b = strip + 27 > ROG_W ? ROG_W : strip + 27;
          rog(C_WHT, a, b, 0, ROG_H);
          if (b < ROG_W) rog(C_HOT, b, b + 4 > ROG_W ? ROG_W : b + 4, 0, ROG_H);
          strip = b;
          if (b >= ROG_W) {
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
  enum Step : uint8_t { BG, SPRITE, ANIM, GLITCH, BLACK, ROG_DIM, ROG_WAIT, ROG_SWEEP, ROG_HOLD, CLEAR, DONE };
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

  // One glitch frame every 50 ms, 14 frames.
  void glitch(uint32_t now) {
    if (now - at < 50) return;
    at = now;
    uint8_t f = stage++;
    if (inverted) {   // a flash lasts one frame
      tft->invertDisplay(invNormal);
      inverted = false;
    }
    if (f < 10) {
      for (uint8_t k = 0; k < 2; k++) {   // tear: redraw a band of sprite rows shifted sideways
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
      static const uint16_t NOISE[4] = {C_HOT, C_WHT, C_DG, C_MAR};
      for (uint8_t k = 0; k < 3; k++) {   // scanline noise
        int16_t y = random(H), x = random(W / 2), w = 20 + random(W);
        if (x + w > W) w = W - x;
        tft->fillRect(x, y, w, 1 + random(3), NOISE[random(4)]);
      }
      if (f == 3 || f == 7) {
        tft->invertDisplay(!invNormal);
        inverted = true;
      }
    } else if (f < 14) {   // signal lost: black bands eat the picture
      for (uint8_t k = 0; k < 4; k++) {
        int16_t y = random(H), h = 8 + random(17);
        if (y + h > H) h = H - y;
        tft->fillRect(0, y, W, h, 0x0000);
      }
      tft->fillRect(0, random(H), W, 2, C_HOT);
    } else {
      tft->invertDisplay(invNormal);
      strip = 0;
      step = BLACK;
    }
  }

  // Streams logo rows [row0, row0 + rows) restricted to columns [a, b): logo pixels in fg, rest black.
  void rog(uint16_t fg, uint16_t a, uint16_t b, uint16_t row0, uint16_t rows) {
    if (row0 + rows > ROG_H) rows = ROG_H - row0;
    if (b <= a || !rows) return;
    uint16_t i = 0;
    for (uint16_t r = 0; r < row0; r++)            // skip to row0
      for (uint16_t x = 0; x < ROG_W;) x += pgm_read_byte(ROG_RLE + i++);
    tft->startWrite();
    tft->setAddrWindow(lx + a, ly + row0, b - a, rows);
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
