#pragma once
// Boot sequence, ~5 s, no text except the version under the ROG logo:
//   1. Pingu: one of the penguins in boot_sprite.h (random per boot) goes "noot noot".
//   2. Crash, ~0.8 s, in the middle of the second noot (beak stuck open): the picture jolts sideways (hardware
//      scroll, so the hidden GRAM rows show as a garbage stripe), inverts, tears in wrong colours; then the
//      screen falls apart in 12-row bands in random order, each band landing on its final ROG content
//      (rog_logo.h) while displaced ghost bands of the logo flicker in the rest. Nothing is redrawn twice,
//      so the whole crash costs about one full-screen write at 2 MHz.
//   3. ROG: a red scan passes over the white logo, red underline + firmware version, hold, clear to black.
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
    for (uint8_t i = 0; i < NBANDS; i++) order[i] = i;
    at = now;
  }

  bool active() const { return step != DONE; }
  uint8_t phase() const { return step; }   // for the host preview timeline

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
  enum Step : uint8_t { BG, SPRITE, ANIM, GLITCH, SWEEP, ROG_HOLD, CLEAR, DONE };
  static const uint8_t BAND = 12, NBANDS = 20;   // 20 x 12 rows = 240
  Adafruit_ST7789 *tft;
  const SplashSprite *sp;
  const char *ver;
  bool invNormal, inverted;
  uint16_t W, H, strip;
  uint8_t S;
  int16_t sx, sy, lx, ly;
  Step step = DONE;
  uint16_t idx;
  uint8_t ry, stage, done;
  uint8_t order[NBANDS];   // band resolve order (shuffled); order[0..done) are final
  bool corrupt;            // blit with wrong colours
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

  uint16_t color(uint8_t c) const {
    static const uint16_t BAD[4] = {0xF81F, 0x07E0, C_HOT, 0xFFE0};   // magenta, green, red, yellow
    if (corrupt) return c ? BAD[c & 3] : C_BLK;
    return c ? pgm_read_word(sp->pal + c) : sp->bg;
  }

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

  // Timeline (ms after the sprite is up): noot 600, back 1000, noot 1250, crash 1450 (beak still open).
  void anim(uint32_t now) {
    static const uint16_t T[] = {600, 1000, 1250, 1450};
    if (stage >= 4 || now - at < T[stage]) return;
    if (stage == 3) {
      next(GLITCH, now);
      inverted = corrupt = false;
      done = 0;
      for (uint8_t i = NBANDS - 1; i > 0; i--) {   // shuffle the band order
        uint8_t j = random(i + 1), t = order[i];
        order[i] = order[j];
        order[j] = t;
      }
      return;
    }
    beak(stage == 0 || stage == 2);
    stage++;
  }

  // Hardware vertical scroll (VSCRSADD). Landscape x is the panel's row axis, so this jolts the whole picture
  // sideways with wrap-around; costs one command. 0 = normal. SWRESET (re-init) also resets it.
  void scroll(int16_t dx) {
    uint16_t v = (uint16_t)((dx % 320 + 320) % 320);
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    tft->sendCommand(0x37, b, 2);
  }

  void flash(bool on) {
    tft->invertDisplay(on ? !invNormal : invNormal);
    inverted = on;
  }

  // A band of 1-3 sprite rows redrawn shifted sideways (phase 1 only, before any band is final).
  void tear(bool bad) {
    uint8_t r0 = random(sp->h), n = 1 + random(3);
    if (r0 + n > sp->h) n = sp->h - r0;
    int16_t dx = random(-30, 31), ox = sx + dx, wpx = sp->w * S;
    if (ox < 0) ox = 0;
    if (ox + wpx > W) ox = W - wpx;
    if (ox > sx) tft->fillRect(sx, sy + r0 * S, ox - sx, n * S, sp->bg);                 // uncovered gap
    if (ox < sx) tft->fillRect(ox + wpx, sy + r0 * S, sx - ox, n * S, sp->bg);
    corrupt = bad;
    idx = rowOffset(r0);
    ry = r0;
    blit(sp->base, ox, sy, sp->w, r0 + n, n);
    corrupt = false;
  }

  // Rows [y0, y0 + n) of the screen intersected with the logo rows, as logo rows (r0, rows). False if none.
  bool logoRows(int16_t y0, int16_t n, uint16_t &r0, uint16_t &rows) const {
    int16_t a = y0 > ly ? y0 : ly, b = y0 + n < ly + ROG_H ? y0 + n : ly + ROG_H;
    if (b <= a) return false;
    r0 = a - ly;
    rows = b - a;
    return true;
  }

  // Band b to its final ROG content: black, with the logo rows in white.
  void resolve(uint8_t b) {
    int16_t y0 = b * BAND;
    uint16_t r0, rows;
    if (!logoRows(y0, BAND, r0, rows)) {
      tft->fillRect(0, y0, W, BAND, C_BLK);
      return;
    }
    if ((int16_t)(ly + r0) > y0) tft->fillRect(0, y0, W, ly + r0 - y0, C_BLK);
    if (ly + r0 + rows < y0 + BAND) tft->fillRect(0, ly + r0 + rows, W, y0 + BAND - ly - r0 - rows, C_BLK);
    tft->fillRect(0, ly + r0, lx, rows, C_BLK);
    tft->fillRect(lx + ROG_W, ly + r0, W - lx - ROG_W, rows, C_BLK);
    rog(C_WHT, 0, ROG_W, r0, rows, 0);
  }

  // Scanline noise inside band b (only bands not yet final, so nothing has to be cleaned up later).
  void noise(uint8_t b) {
    static const uint16_t NOISE[4] = {C_HOT, C_WHT, C_DG, 0xF81F};
    int16_t y = b * BAND + random(BAND - 2), x = random(W / 2), w = 30 + random(W);
    if (x + w > W) w = W - x;
    tft->fillRect(x, y, w, 1 + random(2), NOISE[random(4)]);
  }

  void glitch(uint32_t now) {
    if (now - at < 25) return;
    at = now;
    uint8_t f = stage++;
    if (inverted) flash(false);   // a flash lasts one frame
    if (f < 5) {                  // phase 1: the picture breaks
      scroll(f == 2 ? 0 : (random(2) ? 1 : -1) * (20 + random(60)));
      tear(f != 1);
      if (f) tear(random(2));
      noise(random(NBANDS));
      if (f == 0 || f == 4) flash(true);
      return;
    }
    if (done >= NBANDS) {         // all bands final: picture back in place
      scroll(0);
      flash(false);
      next(SWEEP, now);
      return;
    }
    // phase 2: two bands land on their final content, a ghost of the logo flickers in one that hasn't
    uint8_t left = NBANDS - done;
    scroll(left > 4 ? random(-3 * left, 3 * left + 1) : 0);
    for (uint8_t k = 0; k < 2 && done < NBANDS; k++) resolve(order[done++]);
    if (done < NBANDS) {
      uint8_t b = order[done + random(NBANDS - done)];
      uint16_t r0, rows;
      if (logoRows(b * BAND, BAND, r0, rows))
        rog(random(4) ? C_WHT : C_HOT, 0, ROG_W, r0, rows, random(-4 * left, 4 * left + 1));
      noise(order[done + random(NBANDS - done)]);
    }
    if (f == 9) flash(true);
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
