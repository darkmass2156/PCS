#include "screens.h"
#include <math.h>

// ---------------- helpers ----------------
static uint32_t hs(const char *s, uint32_t h = 2166136261UL) {
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619UL;
  }
  return h;
}
static uint32_t hn(uint32_t v, uint32_t h = 2166136261UL) {
  for (uint8_t i = 0; i < 4; i++) {
    h ^= (uint8_t)(v >> (8 * i));
    h *= 16777619UL;
  }
  return h;
}
static uint32_t key(uint32_t h) { return h | 1; }   // 0 = "not drawn yet"

static int16_t clampi(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }

// "%5.1f"-style fixed cells for the segment digits: " 12.3", "-25.5", " --.-"
static char *seg5(char *out, float v) {
  if (isnan(v)) strcpy(out, " --.-");
  else {
    if (v > 99.9f) v = 99.9f;
    if (v < -99.9f) v = -99.9f;
    dtostrf(v, 5, 1, out);
  }
  return out;
}

// "25.1\xF8C", "42.1%" (cp437 248 = degree sign)
static char *degC(char *out, float v) {
  char t[10];
  fmt1(t, v, !isnan(v));
  strcpy(out, t);
  if (!isnan(v)) strcat(out, "\xF8" "C");
  return out;
}
static char *pct(char *out, float v) {
  fmt1(out, v, !isnan(v));
  if (!isnan(v)) strcat(out, "%");
  return out;
}
static char *signed1(char *out, float v, const char *unit) {
  if (isnan(v)) {
    strcpy(out, "--");
    return out;
  }
  char t[10];
  dtostrf(v, 0, 1, t);
  out[0] = v >= 0 ? '+' : 0;
  out[1] = 0;
  strcat(out, t);
  strcat(out, unit);
  return out;
}
static const char *fanText(char *out, const UiData &d) {
  if (d.fanPct == 0) return "STOPPED";
  snprintf(out, 8, "%u%%", d.fanPct);
  return out;
}
static const char *valveText(const UiData &d) { return d.homing ? "HOMING" : d.mixing ? "MIXING" : "BYPASS"; }
static const char *potText(char *out, const UiData &d) {
  if (!d.potOk) return "--";
  snprintf(out, 10, "%ld W", (long)lround(d.potW));
  return out;
}

static const char *const FAULT_NAMES[] = {
  "NTC T1", "NTC T2", "NTC T3", "NO DEW POINT", "T2 LOW", "T1 LOW", "FUSE P2", "FUSE VALVES", "P2 STALL"};

const char *Screens::faultText(const UiData &d, uint8_t &count) const {
  count = 0;
  for (uint16_t m = d.faults; m; m &= m - 1) count++;
  if (!count) return "";
  uint8_t n = (now / 2000) % count;
  for (uint8_t i = 0; i < 9; i++) {
    if (!(d.faults & (1 << i))) continue;
    if (n-- == 0) return FAULT_NAMES[i];
  }
  return "";
}

// ---------------- frame ----------------
void Screens::begin(Adafruit_ST7789 *t, uint16_t w, uint16_t h, const char *version) {
  gfx = t;
  W = w;
  H = h;
  ver = version;
  mode = 0;
  memset(hist, 0, sizeof(hist));
  samples = drawnSamples = 0;
}

uint16_t Screens::bg() const { return mode == 3 ? C_LG : C_BLK; }

uint8_t Screens::widgetCount() const { return mode == 3 ? 23 : 16; }

void Screens::update(uint32_t t, const UiData &d) {
  now = t;
  if (d.mode != mode) {
    mode = d.mode;
    phase = CLEAR;
    step = 0;
  }
  switch (phase) {
    case CLEAR:   // mode change: clear in strips (a full fill is ~0.5 s at 2 MHz)
      gfx->fillRect(0, step, W, 24, bg());
      step += 24;
      if (step >= H) {
        phase = STATIC;
        step = 0;
      }
      return;
    case STATIC:
      if (!staticStep(step++)) {
        phase = mode == 3 ? CHART : LIVE;
        step = 0;
        memset(cache, 0, sizeof(cache));
        memset(segPrev, 0, sizeof(segPrev));
        memset(barPrev, 0xFF, sizeof(barPrev));
        stripKind = 0;
        next = 0;
      }
      return;
    case CHART:   // mode 3: draw the stored history, 17 columns per call
      for (uint8_t k = 0; k < 17 && step < CHART_W; k++) chartColumn(step++);
      if (step >= CHART_W) {
        drawnSamples = samples;
        gfx->drawFastVLine(5 + samples % CHART_W, 161, 65, C_DG);
        phase = LIVE;
      }
      return;
    case LIVE: {
      uint8_t n = widgetCount();
      for (uint8_t k = 0; k < n; k++) {
        uint8_t i = (next + k) % n;
        uint32_t kv = widget(i, d, false);
        if (kv != cache[i]) {
          cache[i] = widget(i, d, true);
          next = i + 1;
          return;
        }
      }
      return;
    }
  }
}

// ---------------- chart ----------------
static uint8_t enc(float v) {
  if (isnan(v)) return 0;
  return clampi(lround((v + 10) * 5), 1, 250);
}
static int16_t chartY(uint8_t e) {   // 0..35 °C over rows 161..225
  float v = e / 5.0f - 10;
  return clampi(lround(225 - v * 64 / 35), 161, 225);
}

void Screens::sample(const UiData &d) {
  uint16_t c = samples % CHART_W;
  hist[0][c] = enc(d.dp);
  hist[1][c] = enc(d.tset);
  hist[2][c] = enc(d.valve);
  samples++;
}

void Screens::chartColumn(uint16_t c) {
  int16_t x = 5 + c;
  gfx->drawFastVLine(x, 161, 65, C_WHT);
  if (c % 2 == 0)
    for (uint8_t q = 1; q < 4; q++) gfx->drawPixel(x, 161 + q * 16, C_LG);
  bool has = samples > c || samples >= CHART_W;
  if (!has) return;
  uint16_t head = (samples - 1) % CHART_W;
  bool joinPrev = c > 0 && c != (head + 1) % CHART_W;   // don't join the oldest column to the newest
  static const uint16_t COL[3] = {C_MAR, C_HOT, C_BLK};
  for (uint8_t s = 0; s < 3; s++) {
    uint8_t e = hist[s][c];
    if (!e) continue;
    if (s == 1 && c % 4 >= 2) continue;   // T_set dashed
    int16_t y = chartY(e), y0 = y;
    uint8_t ep = joinPrev ? hist[s][c - 1] : 0;
    if (ep) y0 = chartY(ep);
    int16_t a = y < y0 ? y : y0, b = y < y0 ? y0 : y;
    gfx->drawFastVLine(x, a, b - a + 1, COL[s]);
  }
}

// ---------------- shared widgets ----------------
uint32_t Screens::heartbeat(int16_t x, int16_t y, uint16_t on, uint16_t off, bool draw) {
  bool lit = (now / 1000) & 1;
  if (draw) gfx->fillCircle(x, y, 2, lit ? on : off);
  return key(lit + 2);
}

uint32_t Screens::slantBar(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t nseg, uint8_t lit,
                           uint8_t redFrom, uint8_t redTo, bool tipRed, bool draw) {
  if (!draw) return key(hn(lit, slot + 7));
  uint8_t prev = barPrev[slot];
  int16_t sw = (w - (nseg - 1) * 2) / nseg;
  float slope = 0.5f;
  for (uint8_t i = 0; i < nseg; i++) {
    uint16_t cNew = i < lit ? (((tipRed && i == lit - 1) || (i >= redFrom && i < redTo)) ? C_HOT : C_WHT) : C_DG;
    if (prev != 0xFF) {
      uint16_t cOld = i < prev ? (((tipRed && i == prev - 1) || (i >= redFrom && i < redTo)) ? C_HOT : C_WHT) : C_DG;
      if (cOld == cNew) continue;
    }
    fillSlant(x + i * (sw + 2), y, sw, h, slope, cNew);
  }
  barPrev[slot] = lit;
  return key(hn(lit, slot + 7));
}

// Mode 1 bottom strip (y 178..239): distance to dew with bar, or a banner.
uint32_t Screens::bottomStrip1(const UiData &d, bool draw) {
  char b[24], t[48];
  uint8_t kind = d.no12v ? 2 : d.faults ? 3 : 1;
  uint8_t n = 0;
  const char *f = faultText(d, n);
  int16_t px = isnan(d.dew) ? 0 : clampi(lround(d.dew / 15 * 270), 0, 270);
  signed1(b, d.dew, "\xF8" "C");
  uint32_t k = key(hs(kind == 3 ? f : b, hn(kind * 1000 + px + n * 7)));
  if (!draw) return k;
  if (kind != stripKind) {
    gfx->fillRect(0, 178, W, 62, kind == 1 ? C_BLK : kind == 2 ? C_DG : C_RED);
    if (kind == 1) {
      ftxt(F_LABEL, 4, 186, C_LG, C_BLK, "DISTANCE TO DEW", 0);
      gfx->drawRect(4, 205, 272, 12, C_DG);
      ftxt(F_LABEL, 4, 222, C_DG, C_BLK, "at dew", 0);
      ftxt(F_LABEL, W - 4 - 60, 222, C_DG, C_BLK, "+15 \xF8" "C", 60, 'r');
    }
    stripKind = kind;
  }
  if (kind == 1) {
    ftxt(F_VALUE, W - 4 - 96, 183, (!isnan(d.dew) && d.dew < 2) ? C_HOT : C_WHT, C_BLK, b, 96, 'r');
    int16_t red = px < 36 ? px : 36;
    gfx->fillRect(5, 206, red, 10, C_RED);
    gfx->fillRect(5 + red, 206, px - red, 10, C_LG);
    gfx->fillRect(5 + px, 206, 270 - px, 10, C_BLK);
  } else if (kind == 2) {
    ftxt(F_VALUE, 4, 201, C_WHT, C_DG, "NO 12V  -  PC OFF", 272, 'c');
  } else {
    snprintf(t, sizeof(t), "FAULT: %s", f);
    ftxt(F_VALUE, 4, 191, C_WHT, C_RED, t, 272, 'c');
    snprintf(t, sizeof(t), "%u active  -  press a button to reset", n);
    ftxt(F_LABEL, 4, 215, C_WHT, C_RED, n > 1 ? t : "press a button to reset", 272, 'c');
  }
  return k;
}

// Mode 2 bottom strip: shift lights, or a banner.
uint32_t Screens::bottomStrip2(const UiData &d, bool draw) {
  char b[24], t[48];
  uint8_t kind = d.no12v ? 2 : d.faults ? 3 : 1;
  uint8_t n = 0;
  const char *f = faultText(d, n);
  uint8_t lit = isnan(d.dew) ? 0 : clampi(lround(d.dew), 0, 12);
  signed1(b, d.dew, "\xF8");
  uint32_t k = key(hs(kind == 3 ? f : b, hn(kind * 1000 + lit)));
  if (!draw) return k;
  if (kind != stripKind) {
    gfx->fillRect(0, 209, W, 27, kind == 1 ? C_BLK : kind == 2 ? C_DG : C_RED);
    if (kind == 1) txt(4, 216, 1, C_WHT, C_BLK, "TO DEW", 6);
    barPrev[3] = 0xFF;
    stripKind = kind;
  }
  if (kind == 1) {
    slantBar(3, 46, 213, 140, 14, 12, lit, 0, 2, false, true);
    txt(W - 4 - 72, 212, 2, lit < 2 ? C_HOT : C_WHT, C_BLK, b, 6, 'r');
  } else if (kind == 2) {
    txt(4, 215, 2, C_WHT, C_DG, "NO 12V - PC OFF", 22, 'c');
  } else {
    snprintf(t, sizeof(t), "FAULT: %s", f);
    txt(4, 215, 2, C_WHT, C_RED, t, 22, 'c');
  }
  return k;
}

// ---------------- static parts ----------------
bool Screens::staticStep(uint16_t i) {
  if (mode == 1) {
    static const char *const LAB[3] = {"OUTSIDE RAD", "LOOP 1 RETURN", "MIXING VALVE"};
    switch (i) {
      case 0:
        ftxt(F_LABEL, 4, 3, C_LG, C_BLK, "STANDARD", 0);
        gfx->drawFastHLine(0, 15, W, C_DG);
        return true;
      case 1:
        for (uint8_t c = 0; c < 3; c++) {
          int16_t x0 = c * 93;
          ftxt(F_LABEL, x0 + 1, 20, C_LG, C_BLK, LAB[c], 91, 'c');
          if (c) gfx->drawFastVLine(x0, 20, 40, C_DG);
        }
        return true;
      case 2:
      case 3: {
        int16_t x0 = i == 2 ? 4 : 142;
        gfx->drawRect(x0, 64, 134, 65, C_DG);
        gfx->fillRect(x0, 64, 134, 13, C_DG);
        ftxt(F_LABEL, x0 + 5, 66, C_WHT, C_DG, i == 2 ? "CASE" : "ROOM", 0);
        ftxt(F_LABEL, x0 + 6, 83, C_LG, C_BLK, "Temp", 0);
        ftxt(F_LABEL, x0 + 6, 99, C_LG, C_BLK, "Humidity", 0);
        ftxt(F_LABEL, x0 + 6, 115, C_LG, C_BLK, "Dew point", 0);
        return true;
      }
      case 4: {
        static const char *const T[4] = {"COOLING", "EXT LOOP", "VALVE", "FAN"};
        for (uint8_t k = 0; k < 4; k++) {
          int16_t x0 = k % 2 ? 142 : 4, y0 = k < 2 ? 134 : 155;
          gfx->drawRect(x0, y0, 134, 20, C_DG);
          ftxt(F_LABEL, x0 + 6, y0 + 5, C_LG, C_BLK, T[k], 0);
        }
        return true;
      }
    }
    return false;
  }
  if (mode == 2) {
    static const char *const LAB[3] = {"OUTSIDE RAD", "LOOP 1 RETURN", "MIXING VALVE"};
    switch (i) {
      case 0:
        gfx->fillRect(0, 0, 100, 15, C_WHT);
        gfx->fillTriangle(100, 0, 111, 0, 100, 14, C_WHT);
        txt(6, 4, 1, C_BLK, C_WHT, "SPORT", 5);
        for (uint8_t k = 0; k < 3; k++) fillSlant(106 + k * 9, 0, 5, 15, 0.6f, k == 2 ? C_HOT : C_DG);
        return true;
      case 1:
        for (uint8_t r = 0; r < 3; r++) {
          int16_t y0 = 22 + r * 38;
          txt(4, y0, 1, C_LG, C_BLK, LAB[r], 13);
          txt(266, y0 + 2, 1, C_LG, C_BLK, "\xF8" "C", 2);
        }
        return true;
      case 2:
        gfx->drawFastHLine(0, 137, W, C_DG);
        gfx->drawFastHLine(0, 169, W, C_DG);
        txt(4, 141, 1, C_WHT, C_BLK, "CASE", 4);
        txt(144, 141, 1, C_WHT, C_BLK, "ROOM", 4);
        txt(4, 177, 1, C_LG, C_BLK, "COOLING", 7);
        return true;
      case 3: {
        static const char *const T[3] = {"EXT", "VALVE", "FAN"};
        for (uint8_t k = 0; k < 3; k++) {
          int16_t x0 = 4 + k * 92;
          gfx->drawLine(x0 + 6, 191, x0 + 88, 191, C_DG);
          gfx->drawLine(x0 + 88, 191, x0 + 82, 206, C_DG);
          gfx->drawLine(x0 + 82, 206, x0, 206, C_DG);
          gfx->drawLine(x0, 206, x0 + 6, 191, C_DG);
          txt(x0 + 10, 195, 1, C_LG, C_BLK, T[k], 5);
        }
        gfx->fillRect(0, 237, W, 2, C_MAR);
        return true;
      }
    }
    return false;
  }
  // mode 3
  switch (i) {
    case 0:
      gfx->fillRect(0, 0, W, 14, C_MAR);
      txt(3, 3, 1, C_WHT, C_MAR, "PROCESS CONTROL", 15);
      txt(W - 3 - 6 * strlen(ver), 3, 1, C_LG, C_MAR, ver, strlen(ver));
      return true;
    case 1:   // P&ID: T1 -> P2 -> V1 -> rad (T3) -> V2 <- bypass; V2 -> T2 -> PC
      gfx->drawRect(4, 20, 40, 12, C_BLK);
      gfx->drawFastHLine(44, 26, 16, C_BLK);
      gfx->drawCircle(66, 26, 6, C_BLK);
      gfx->drawFastHLine(72, 26, 20, C_BLK);
      gfx->drawTriangle(92, 20, 92, 32, 104, 26, C_BLK);
      gfx->drawTriangle(116, 20, 116, 32, 104, 26, C_BLK);
      gfx->drawFastHLine(116, 26, 14, C_BLK);
      gfx->drawRect(130, 20, 35, 12, C_BLK);
      gfx->drawFastVLine(147, 32, 27, C_BLK);
      gfx->drawLine(147, 58, 116, 66, C_BLK);
      gfx->drawFastVLine(66, 32, 35, C_BLK);
      gfx->drawFastHLine(66, 66, 26, C_BLK);
      gfx->drawTriangle(92, 60, 92, 72, 104, 66, C_BLK);
      gfx->drawTriangle(116, 60, 116, 72, 104, 66, C_BLK);
      gfx->fillTriangle(98, 80, 110, 80, 104, 66, C_WHT);
      gfx->drawTriangle(98, 80, 110, 80, 104, 66, C_BLK);
      gfx->drawFastVLine(104, 80, 12, C_BLK);
      gfx->drawRect(84, 92, 40, 12, C_BLK);
      txt(6, 34, 1, C_DG, C_LG, "T1 ret", 6);
      txt(61, 34, 1, C_DG, C_LG, "P2", 2);
      txt(98, 34, 1, C_DG, C_LG, "V1", 2);
      txt(130, 34, 1, C_DG, C_LG, "T3 rad", 6);
      txt(70, 56, 1, C_DG, C_LG, "byp", 3);
      txt(128, 94, 1, C_DG, C_LG, "T2>PC", 5);
      return true;
    case 2: {
      static const char *const L[10] = {"DP", "MAX", "SET", "OUT", "POT", "P2", "FAN", "FUSE", "FLT", "ALM"};
      gfx->drawFastVLine(168, 16, 108, C_DG);
      for (uint8_t k = 0; k < 10; k++) txt(171, 18 + k * 10, 1, C_MAR, C_LG, L[k], 4);
      return true;
    }
    case 3:
      gfx->fillRect(0, 126, W, 25, C_DG);
      return true;
    case 4:
      txt(4, 152, 1, C_BLK, C_LG, "T2", 2);
      txt(22, 152, 1, C_HOT, C_LG, "Tset", 4);
      txt(52, 152, 1, C_MAR, C_LG, "DP", 2);
      txt(W - 4 - 6 * 19, 152, 1, C_DG, C_LG, "0-35\xF8" "C  1 px = 1 s", 19, 'r');
      gfx->drawRect(4, 160, 274, 67, C_DG);
      return true;
  }
  return false;
}

// ---------------- widgets ----------------
uint32_t Screens::widget(uint8_t i, const UiData &d, bool draw) {
  char b[48], t[24];
  if (mode == 1) {
    if (i == 0) return heartbeat(272, 7, C_WHT, C_DG, draw);
    if (i <= 3) {   // big temperatures, "12.3°"
      float v = i == 1 ? d.rad : i == 2 ? d.loop : d.valve;
      fmt1(b, v, !isnan(v));
      if (!isnan(v)) strcat(b, "\xF8");
      if (draw) ftxt(F_BIG, (i - 1) * 93 + 2, 34, C_WHT, C_BLK, b, 89, 'c');
      return key(hs(b, i));
    }
    if (i <= 9) {
      uint8_t k = i - 4, row = k % 3;
      bool room = k >= 3;
      float v = row == 0 ? (room ? d.roomT : d.caseT) : row == 1 ? (room ? d.roomRH : d.caseRH) : (room ? d.roomDP : d.caseDP);
      if (row == 1) pct(b, v); else degC(b, v);
      if (draw) ftxt(F_VALUE, (room ? 142 : 4) + 58, 80 + row * 16, C_WHT, C_BLK, b, 71, 'r');
      return key(hs(b, i));
    }
    if (i <= 13) {
      uint8_t k = i - 10;
      const char *v = k == 0 ? potText(t, d) : k == 1 ? (d.extOpen ? "OPEN" : "CLOSED") : k == 2 ? valveText(d) : fanText(t, d);
      static const char *const T[4] = {"COOLING", "EXT LOOP", "VALVE", "FAN"};   // same as the static labels
      if (draw) {
        int16_t x0 = k % 2 ? 142 : 4, vx = x0 + 6 + ftWidth(F_LABEL, T[k]) + 4;   // value box right of its label
        ftxt(F_VALUE, vx, (k < 2 ? 134 : 155) + 2, C_WHT, C_BLK, v, x0 + 130 - vx, 'r');
      }
      return key(hs(v, i));
    }
    if (i == 14) return bottomStrip1(d, draw);
    snprintf(b, sizeof(b), "%s%s", d.state, d.sim ? " SIM" : "");   // 15: state next to the title
    if (draw) ftxt(F_LABEL, 66, 3, C_DG, C_BLK, b, 90);
    return key(hs(b, i));
  }

  if (mode == 2) {
    static const SegStyle SEG2 = {17, 32, 4, 4, 0.18f};
    if (i == 0) return heartbeat(272, 6, C_WHT, C_DG, draw);
    if (i <= 3) {   // temperature bars, -10..40 °C
      float v = i == 1 ? d.rad : i == 2 ? d.loop : d.valve;
      uint8_t lit = isnan(v) ? 0 : clampi(lround((v + 10) / 5), 0, 10);
      return slantBar(i - 1, 4, 22 + (i - 1) * 38 + 13, 118, 14, 10, lit, 0, 0, true, draw);
    }
    if (i <= 6) {
      float v = i == 4 ? d.rad : i == 5 ? d.loop : d.valve;
      seg5(b, v);
      if (draw) segText(168, 22 + (i - 4) * 38 + 2, SEG2, b, segPrev[i - 4], C_WHT, C_BLK);
      return key(hs(b, i));
    }
    if (i <= 8) {
      bool room = i == 8;
      char a[10], c[10], e[10];
      fmt1(a, room ? d.roomT : d.caseT, true);
      fmt1(c, room ? d.roomRH : d.caseRH, true);
      fmt1(e, room ? d.roomDP : d.caseDP, true);
      snprintf(b, sizeof(b), "%s\xF8 %s%% dp %s\xF8", a, c, e);
      if (draw) txt(room ? 144 : 4, 155, 1, C_WHT, C_BLK, b, 22);
      return key(hs(b, i));
    }
    if (i == 9) {   // cooling power meter, 0..1000 W
      uint8_t lit = d.potOk ? clampi(lround(d.potW / 1000 * 15), 0, 15) : 0;
      return slantBar(4, 52, 177, 140, 9, 15, lit, 0, 0, true, draw);
    }
    if (i == 10) {
      const char *v = potText(t, d);
      if (draw) txt(W - 4 - 72, 174, 2, C_WHT, C_BLK, v, 6, 'r');
      return key(hs(v, i));
    }
    if (i <= 13) {
      uint8_t k = i - 11;
      const char *v = k == 0 ? (d.extOpen ? "OPEN" : "CLOSED") : k == 1 ? valveText(d) : fanText(t, d);
      uint8_t w = k == 2 ? 7 : 6;
      if (draw) txt(4 + k * 92 + 80 - w * 6, 195, 1, C_WHT, C_BLK, v, w, 'r');
      return key(hs(v, i));
    }
    if (i == 14) return bottomStrip2(d, draw);
    snprintf(b, sizeof(b), "%s%s", d.state, d.sim ? " SIM" : "");   // 15: state in the tab
    if (draw) txt(42, 4, 1, C_DG, C_WHT, b, 9);
    return key(hs(b, i));
  }

  // ---- mode 3 ----
  switch (i) {
    case 0:
      snprintf(b, sizeof(b), "M3 %s%s", d.state, d.sim ? " SIM" : "");
      if (draw) txt(100, 3, 1, C_WHT, C_MAR, b, 12);
      return key(hs(b, i));
    case 1:
      return heartbeat(W - 3 - 6 * strlen(ver) - 8, 6, C_WHT, C_MAR, draw);
    case 2:
      fmt1(b, d.loop, !isnan(d.loop));
      if (draw) txt(6, 22, 1, C_BLK, C_WHT, b, 6, 'c');
      return key(hs(b, i));
    case 3:
      fmt1(b, d.rad, !isnan(d.rad));
      if (draw) {
        gfx->fillRect(131, 21, 33, 10, C_DG);
        txt(132, 22, 1, C_WHT, C_DG, b, 5, 'c');
      }
      return key(hs(b, i));
    case 4:
      fmt1(b, d.valve, !isnan(d.valve));
      if (draw) {
        gfx->fillRect(85, 93, 38, 10, C_RED);
        txt(86, 94, 1, C_WHT, C_RED, b, 6, 'c');
      }
      return key(hs(b, i));
    case 5: {   // V1: filled white when open
      uint16_t c = d.extOpen ? C_WHT : C_DG;
      if (draw) {
        gfx->fillTriangle(93, 22, 93, 30, 102, 26, c);
        gfx->fillTriangle(115, 22, 115, 30, 106, 26, c);
      }
      return key(c + 11);
    }
    case 6: {   // P2: filled white when running
      uint16_t c = d.p2On ? C_WHT : C_DG;
      if (draw) {
        gfx->fillCircle(66, 26, 5, c);
        gfx->setTextSize(1);
        gfx->setTextColor(C_BLK);
        gfx->setCursor(64, 23);
        gfx->print('P');
      }
      return key(c + 13);
    }
    case 7:
      fmt1(t, d.v2deg, d.v2homed);
      snprintf(b, sizeof(b), "V2 %s\xF8%s", t, d.v2homed ? "" : "?");
      if (draw) txt(122, 70, 1, C_MAR, C_LG, b, 9);
      return key(hs(b, i));
    case 8: {
      char a[10], c[10];
      fmt1(a, d.tset, !isnan(d.tset));
      signed1(c, d.valve - d.tset, "");
      snprintf(b, sizeof(b), "Tset %s  dev %s", a, c);
      if (draw) txt(4, 112, 1, C_BLK, C_LG, b, 26);
      return key(hs(b, i));
    }
    case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: {
      char a[10], c[10], e[10];
      switch (i) {
        case 9:
          snprintf(b, sizeof(b), "%s/%s/%s", fmt1(a, d.dpA, !isnan(d.dpA)), fmt1(c, d.dpB, !isnan(d.dpB)),
                   fmt1(e, d.dpR, !isnan(d.dpR)));
          break;
        case 10: snprintf(b, sizeof(b), "%s n=%u", fmt1(a, d.dp, d.dpValid > 0), d.dpValid); break;
        case 11: fmt1(b, d.tset, !isnan(d.tset)); break;
        case 12: fmt1(b, d.tout, d.toutOk); break;
        case 13: snprintf(b, sizeof(b), "%sK %s", fmt1(a, d.potK, d.potOk), potText(c, d)); break;
        case 14: if (d.p2On) snprintf(b, sizeof(b), "%u%% %u", d.p2Pct, d.rpmP2); else strcpy(b, "OFF"); break;
        case 15: snprintf(b, sizeof(b), "%u%% %u/%u", d.fanPct, d.rpmF1, d.rpmF2); break;
        case 16: snprintf(b, sizeof(b), "0x%X", d.fuses); break;
        case 17: snprintf(b, sizeof(b), "0x%03X", d.faults); break;
        default: snprintf(b, sizeof(b), "0x%03X", d.alarms); break;
      }
      if (draw) txt(W - 3 - 78, 18 + (i - 9) * 10, 1, (i == 17 && d.faults) ? C_HOT : C_BLK, C_LG, b, 13, 'r');
      return key(hs(b, i));
    }
    case 19:
    case 20: {
      bool room = i == 20;
      char a[10], c[10], e[10];
      fmt1(a, room ? d.roomT : d.caseT, true);
      fmt1(c, room ? d.roomRH : d.caseRH, true);
      fmt1(e, room ? d.roomDP : d.caseDP, true);
      snprintf(b, sizeof(b), "%s  %s\xF8" "C  %s%%RH  dp %s", room ? "ROOM" : "CASE", a, c, e);
      if (draw) txt(4, room ? 140 : 129, 1, C_WHT, C_DG, b, 45);
      return key(hs(b, i));
    }
    case 21:   // chart: new samples since the last draw, then the sweep cursor
      if (draw) {
        if (samples - drawnSamples >= CHART_W) drawnSamples = samples - CHART_W;
        while (drawnSamples < samples) chartColumn(drawnSamples++ % CHART_W);
        gfx->drawFastVLine(5 + samples % CHART_W, 161, 65, C_DG);
      }
      return key(hn(samples, i));
    default: {   // 22: bottom line, or the fault
      uint8_t n = 0;
      const char *f = faultText(d, n);
      if (d.faults) snprintf(b, sizeof(b), "FAULT %s (%u)  0x%03X", f, n, d.faults);
      else {
        char a[12], c[10];
        signed1(a, d.dew, "\xF8" "C");
        fmt1(c, d.v2deg, d.v2homed);
        snprintf(b, sizeof(b), "TO DEW %s  V2 %s\xF8/90  %s", a, c, d.no12v ? "NO 12V" : valveText(d));
      }
      if (draw) {
        uint16_t bgc = d.faults ? C_RED : C_LG;
        gfx->fillRect(0, 228, W, 12, bgc);
        txt(4, 230, 1, d.faults ? C_WHT : C_BLK, bgc, b, 45);
      }
      return key(hs(b, i));
    }
  }
}
