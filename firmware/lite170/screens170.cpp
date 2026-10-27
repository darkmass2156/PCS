#include "screens170.h"
#include <math.h>

// ---------------- helpers (as screens.cpp) ----------------
// Mode 3 chart: frame at y 128..156, plot rows CH_TOP..CH_BOT
#define CH_TOP 129
#define CH_BOT 155

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

// "25.1\xF8C", "42.1%" (cp437 248 = degree sign)
static char *degC(char *out, float v) {
  fmt1(out, v, !isnan(v));
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
// Mode 3 text: monospaced, `chars` cells of 7 px.
static void mono(int16_t x, int16_t y, uint16_t fg, uint16_t bg, const char *s, uint8_t chars, char align = 'l') {
  ftxt(F_MONO, x, y, fg, bg, s, chars * 7, align);
}
static const char *const CHIP2[3] = {"EXT", "V2", "FAN"};               // Mode 2 chips
static const char *const TILE1[4] = {"COOLING", "EXT LOOP", "VALVE", "FAN"};   // Mode 1 tiles
static const char *const BIG[3] = {"OUTSIDE RAD", "LOOP 1 RETURN", "MIXING VALVE"};

static const char *const FAULT_NAMES[] = {
  "NTC T1", "NTC T2", "NTC T3", "NO DEW POINT", "T2 LOW", "T1 LOW", "FUSE P2", "FUSE VALVES", "P2 STALL"};

const char *Screens170::faultText(const UiData &d, uint8_t &count) const {
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
void Screens170::begin(Adafruit_ST7789 *t, const char *version) {
  gfx = t;
  ver = version;
  mode = 0;
  memset(hist, 0, sizeof(hist));
  samples = drawnSamples = 0;
}

uint16_t Screens170::bg() const { return mode == 3 ? C_LG : C_BLK; }

uint8_t Screens170::widgetCount() const { return mode == 3 ? 21 : mode == 2 ? 14 : 16; }

void Screens170::update(uint32_t t, const UiData &d) {
  now = t;
  if (d.mode != mode) {
    mode = d.mode;
    phase = CLEAR;
    step = 0;
  }
  switch (phase) {
    case CLEAR:   // mode change: clear in strips
      gfx->fillRect(0, step, W, step + 24 > H ? H - step : 24, bg());
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
        memset(barPrev, 0xFF, sizeof(barPrev));
        stripKind = 0;
        next = 0;
      }
      return;
    case CHART:   // mode 3: draw the stored history, 20 columns per call
      for (uint8_t k = 0; k < 20 && step < CHART170_W; k++) chartColumn(step++);
      if (step >= CHART170_W) {
        drawnSamples = samples;
        gfx->drawFastVLine(5 + samples % CHART170_W, CH_TOP, CH_BOT - CH_TOP + 1, C_DG);
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
static int16_t chartY(uint8_t e) {   // 0..35 °C over rows CH_TOP..CH_BOT
  float v = e / 5.0f - 10;
  return clampi(lround(CH_BOT - v * (CH_BOT - CH_TOP) / 35), CH_TOP, CH_BOT);
}

void Screens170::sample(const UiData &d) {
  uint16_t c = samples % CHART170_W;
  hist[0][c] = enc(d.dp);
  hist[1][c] = enc(d.tset);
  hist[2][c] = enc(d.valve);
  samples++;
}

void Screens170::chartColumn(uint16_t c) {
  int16_t x = 5 + c;
  gfx->drawFastVLine(x, CH_TOP, CH_BOT - CH_TOP + 1, C_WHT);
  if (c % 2 == 0)
    for (uint8_t q = 1; q < 4; q++) gfx->drawPixel(x, CH_TOP + q * (CH_BOT - CH_TOP) / 4, C_LG);
  bool has = samples > c || samples >= CHART170_W;
  if (!has) return;
  uint16_t head = (samples - 1) % CHART170_W;
  bool joinPrev = c > 0 && c != (head + 1) % CHART170_W;   // don't join the oldest column to the newest
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
uint32_t Screens170::heartbeat(int16_t x, int16_t y, uint16_t on, uint16_t off, bool draw) {
  bool lit = (now / 1000) & 1;
  if (draw) gfx->fillCircle(x, y, 2, lit ? on : off);
  return key(lit + 2);
}

uint32_t Screens170::slantBar(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t nseg, uint8_t lit,
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

// Mode 1 bottom strip (y 139..169): distance to dew with bar, or a banner.
uint32_t Screens170::strip1(const UiData &d, bool draw) {
  char b[24], t[48];
  uint8_t kind = d.no12v ? 2 : d.faults ? 3 : 1;
  uint8_t n = 0;
  const char *f = faultText(d, n);
  int16_t px = isnan(d.dew) ? 0 : clampi(lround(d.dew / 15 * 310), 0, 310);
  signed1(b, d.dew, "\xF8" "C");
  uint32_t k = key(hs(kind == 3 ? f : b, hn(kind * 1000 + px + n * 7)));
  if (!draw) return k;
  if (kind != stripKind) {
    gfx->fillRect(0, 139, W, 31, kind == 1 ? C_BLK : kind == 2 ? C_DG : C_RED);
    if (kind == 1) {
      ftxt(F_LABEL, 4, 143, C_LG, C_BLK, "DISTANCE TO DEW   (0 ... +15 \xF8" "C)", 0);
      gfx->drawRect(4, 158, 312, 10, C_DG);
    }
    stripKind = kind;
  }
  if (kind == 1) {
    ftxt(F_VALUE, W - 4 - 96, 140, (!isnan(d.dew) && d.dew < 2) ? C_HOT : C_WHT, C_BLK, b, 96, 'r');
    int16_t red = px < 41 ? px : 41;   // first 2 °C red
    gfx->fillRect(5, 159, red, 8, C_RED);
    gfx->fillRect(5 + red, 159, px - red, 8, C_LG);
    gfx->fillRect(5 + px, 159, 310 - px, 8, C_BLK);
  } else if (kind == 2) {
    ftxt(F_VALUE, 4, 147, C_WHT, C_DG, "NO 12V  -  PC OFF", 312, 'c');
  } else {
    snprintf(t, sizeof(t), "FAULT: %s", f);
    ftxt(F_VALUE, 4, 140, C_WHT, C_RED, t, 312, 'c');
    snprintf(t, sizeof(t), "%u active  -  press a button to reset", n);
    ftxt(F_LABEL, 4, 157, C_WHT, C_RED, n > 1 ? t : "press a button to reset", 312, 'c');
  }
  return k;
}

// Mode 2 bottom strip (y 140..169): the three chips, or a banner.
uint32_t Screens170::strip2(const UiData &d, bool draw) {
  char t2[10], t[48];
  const char *v[3] = {d.extOpen ? "OPEN" : "CLOSED", valveText(d), fanText(t2, d)};
  uint8_t kind = d.no12v ? 2 : d.faults ? 3 : 1;
  uint8_t n = 0;
  const char *f = faultText(d, n);
  uint32_t h = hn(kind);
  if (kind == 3) h = hs(f, h);
  else for (uint8_t k = 0; k < 3; k++) h = hs(v[k], h);
  if (!draw) return key(h);
  if (kind != stripKind) {
    gfx->fillRect(0, 140, W, 30, kind == 1 ? C_BLK : kind == 2 ? C_DG : C_RED);
    if (kind == 1) {
      for (uint8_t k = 0; k < 3; k++) {
        int16_t x0 = 4 + k * 106;
        gfx->drawLine(x0 + 6, 143, x0 + 98, 143, C_DG);
        gfx->drawLine(x0 + 98, 143, x0 + 92, 158, C_DG);
        gfx->drawLine(x0 + 92, 158, x0, 158, C_DG);
        gfx->drawLine(x0, 158, x0 + 6, 143, C_DG);
        ftxt(F_LABEL, x0 + 9, 146, C_LG, C_BLK, CHIP2[k], 0);
      }
      gfx->fillRect(0, 165, W, 2, C_MAR);
    }
    stripKind = kind;
  }
  if (kind == 1) {
    for (uint8_t k = 0; k < 3; k++) {
      int16_t x0 = 4 + k * 106, vx = x0 + 9 + ftWidth(F_LABEL, CHIP2[k]) + 4;   // value box right of its label
      ftxt(F_LABEL, vx, 146, C_WHT, C_BLK, v[k], x0 + 90 - vx, 'r');
    }
  } else if (kind == 2) {
    ftxt(F_VALUE, 4, 147, C_WHT, C_DG, "NO 12V  -  PC OFF", 312, 'c');
  } else {
    snprintf(t, sizeof(t), "FAULT: %s", f);
    ftxt(F_VALUE, 4, 147, C_WHT, C_RED, t, 312, 'c');
  }
  return key(h);
}

// ---------------- static parts ----------------
bool Screens170::staticStep(uint16_t i) {
  if (mode == 1) {
    switch (i) {
      case 0:
        ftxt(F_LABEL, 4, 2, C_LG, C_BLK, "STANDARD", 0);
        gfx->drawFastHLine(0, 14, W, C_DG);
        return true;
      case 1:
        for (uint8_t c = 0; c < 3; c++) {
          int16_t x0 = c * 107;
          ftxt(F_LABEL, x0 + 1, 18, C_LG, C_BLK, BIG[c], 104, 'c');
          if (c) gfx->drawFastVLine(x0, 18, 36, C_DG);
        }
        return true;
      case 2:   // case / room rows: temp, humidity, dew point
        gfx->drawRect(4, 57, 312, 37, C_DG);
        ftxt(F_LABEL, 10, 62, C_LG, C_BLK, "CASE", 0);
        ftxt(F_LABEL, 10, 79, C_LG, C_BLK, "ROOM", 0);
        ftxt(F_LABEL, 228, 62, C_DG, C_BLK, "dp", 0);
        ftxt(F_LABEL, 228, 79, C_DG, C_BLK, "dp", 0);
        return true;
      case 3:
        for (uint8_t k = 0; k < 4; k++) {
          int16_t x0 = k % 2 ? 162 : 4, y0 = k < 2 ? 97 : 117;
          gfx->drawRect(x0, y0, 154, 18, C_DG);
          ftxt(F_LABEL, x0 + 6, y0 + 4, C_LG, C_BLK, TILE1[k], 0);
        }
        return true;
    }
    return false;
  }
  if (mode == 2) {
    switch (i) {
      case 0:
        gfx->fillRect(0, 0, 100, 15, C_WHT);
        gfx->fillTriangle(100, 0, 111, 0, 100, 14, C_WHT);
        ftxt(F_LABEL, 6, 3, C_BLK, C_WHT, "SPORT", 0);
        for (uint8_t k = 0; k < 3; k++) fillSlant(106 + k * 9, 0, 5, 15, 0.6f, k == 2 ? C_HOT : C_DG);
        ftxt(F_LABEL, 142, 3, C_LG, C_BLK, "DEW", 0);
        return true;
      case 1:
        for (uint8_t r = 0; r < 3; r++) {
          int16_t y0 = 18 + r * 30;
          ftxt(F_LABEL, 4, y0, C_LG, C_BLK, BIG[r], 0);
          ftxt(F_LABEL, 300, y0 + 4, C_LG, C_BLK, "\xF8" "C", 0);
        }
        return true;
      case 2:
        gfx->drawFastHLine(0, 108, W, C_DG);
        gfx->drawFastHLine(0, 124, W, C_DG);
        ftxt(F_LABEL, 4, 111, C_LG, C_BLK, "CASE", 0);
        ftxt(F_LABEL, 164, 111, C_LG, C_BLK, "ROOM", 0);
        ftxt(F_LABEL, 4, 129, C_LG, C_BLK, "COOLING", 0);
        return true;
    }
    return false;
  }
  // mode 3: DejaVu Sans Mono 11 px, 7 px per char, 12 rows
  switch (i) {
    case 0:
      gfx->fillRect(0, 0, W, 14, C_MAR);
      mono(3, 1, C_WHT, C_MAR, "PROCESS CONTROL", 15);
      mono(W - 3 - 7 * strlen(ver), 1, C_LG, C_MAR, ver, strlen(ver));
      return true;
    case 1:   // P&ID: T1 -> P2 -> V1 -> rad (T3) -> V2 <- bypass; V2 -> T2 -> PC (as screens.cpp)
      gfx->drawRect(2, 18, 36, 14, C_BLK);          // T1 box
      gfx->drawFastHLine(38, 25, 4, C_BLK);
      gfx->drawCircle(48, 25, 6, C_BLK);            // P2
      gfx->drawFastHLine(54, 25, 10, C_BLK);
      gfx->drawTriangle(64, 19, 64, 31, 76, 25, C_BLK);   // V1
      gfx->drawTriangle(88, 19, 88, 31, 76, 25, C_BLK);
      gfx->drawFastHLine(88, 25, 8, C_BLK);
      gfx->drawRect(96, 18, 38, 14, C_BLK);         // T3 box (rad)
      gfx->drawFastVLine(48, 31, 43, C_BLK);        // bypass down from P2
      gfx->drawFastHLine(48, 74, 16, C_BLK);
      gfx->drawFastVLine(124, 32, 42, C_BLK);       // rad return down
      gfx->drawFastHLine(88, 74, 36, C_BLK);
      gfx->drawTriangle(64, 68, 64, 80, 76, 74, C_BLK);   // V2
      gfx->drawTriangle(88, 68, 88, 80, 76, 74, C_BLK);
      gfx->fillTriangle(70, 88, 82, 88, 76, 74, C_WHT);
      gfx->drawTriangle(70, 88, 82, 88, 76, 74, C_BLK);
      gfx->drawFastVLine(76, 88, 8, C_BLK);
      gfx->drawRect(57, 96, 38, 14, C_BLK);         // T2 box
      mono(2, 34, C_DG, C_LG, "T1", 2);
      mono(41, 34, C_DG, C_LG, "P2", 2);
      mono(69, 34, C_DG, C_LG, "V1", 2);
      mono(96, 34, C_DG, C_LG, "T3", 2);
      mono(51, 58, C_DG, C_LG, "byp", 3);
      mono(98, 97, C_DG, C_LG, ">PC", 3);
      return true;
    case 2: {
      static const char *const L[10] = {"DP", "MAX", "SET", "OUT", "POT", "P2", "FAN", "FLAG", "CASE", "ROOM"};
      gfx->drawFastVLine(150, 16, 110, C_DG);
      for (uint8_t k = 0; k < 10; k++) mono(154, 16 + k * 11, C_MAR, C_LG, L[k], 4);
      return true;
    }
    case 3:   // chart frame; legend next to the bottom line
      gfx->drawRect(4, CH_TOP - 1, 312, CH_BOT - CH_TOP + 3, C_DG);
      mono(255, 157, C_BLK, C_LG, "T2", 2);
      mono(274, 157, C_HOT, C_LG, "Tset", 4);
      mono(305, 157, C_MAR, C_LG, "DP", 2);
      return true;
  }
  return false;
}

// ---------------- widgets ----------------
uint32_t Screens170::widget(uint8_t i, const UiData &d, bool draw) {
  char b[48], t[24];
  if (mode == 1) {
    if (i == 0) return heartbeat(313, 6, C_WHT, C_DG, draw);
    if (i <= 3) {   // big temperatures, "12.3°"
      float v = i == 1 ? d.rad : i == 2 ? d.loop : d.valve;
      fmt1(b, v, !isnan(v));
      if (!isnan(v)) strcat(b, "\xF8");
      if (draw) ftxt(F_BIG, (i - 1) * 107 + 2, 30, C_WHT, C_BLK, b, 102, 'c');
      return key(hs(b, i));
    }
    if (i <= 9) {   // case / room rows
      uint8_t k = i - 4, col = k % 3;
      bool room = k >= 3;
      int16_t y = room ? 75 : 58;
      float v = col == 0 ? (room ? d.roomT : d.caseT) : col == 1 ? (room ? d.roomRH : d.caseRH) : (room ? d.roomDP : d.caseDP);
      if (col == 1) pct(b, v); else degC(b, v);
      if (draw) {
        if (col == 0) ftxt(F_VALUE, 50, y, C_WHT, C_BLK, b, 80, 'r');
        else if (col == 1) ftxt(F_VALUE, 136, y, C_WHT, C_BLK, b, 76, 'r');
        else ftxt(F_VALUE, 244, y, C_WHT, C_BLK, b, 68, 'r');
      }
      return key(hs(b, i));
    }
    if (i <= 13) {   // tiles
      uint8_t k = i - 10;
      const char *v = k == 0 ? potText(t, d) : k == 1 ? (d.extOpen ? "OPEN" : "CLOSED") : k == 2 ? valveText(d) : fanText(t, d);
      if (draw) {
        int16_t x0 = k % 2 ? 162 : 4, vx = x0 + 6 + ftWidth(F_LABEL, TILE1[k]) + 4;   // value box right of its label
        ftxt(F_VALUE, vx, (k < 2 ? 97 : 117) + 1, C_WHT, C_BLK, v, x0 + 150 - vx, 'r');
      }
      return key(hs(v, i));
    }
    if (i == 14) return strip1(d, draw);
    snprintf(b, sizeof(b), "%s%s", d.state, d.sim ? " SIM" : "");   // 15: state next to the title
    if (draw) ftxt(F_LABEL, 66, 2, C_DG, C_BLK, b, 100);
    return key(hs(b, i));
  }

  if (mode == 2) {
    if (i == 0) return heartbeat(313, 7, C_WHT, C_DG, draw);
    if (i <= 3) {   // temperature bars, -10..40 °C
      float v = i == 1 ? d.rad : i == 2 ? d.loop : d.valve;
      uint8_t lit = isnan(v) ? 0 : clampi(lround((v + 10) / 5), 0, 10);
      return slantBar(i - 1, 4, 18 + (i - 1) * 30 + 13, 150, 12, 10, lit, 0, 0, true, draw);
    }
    if (i <= 6) {   // big slanted numbers
      float v = i == 4 ? d.rad : i == 5 ? d.loop : d.valve;
      fmt1(b, v, !isnan(v));
      if (draw) ftxt(F_SPORT, 168, 18 + (i - 4) * 30 + 2, C_WHT, C_BLK, b, 128, 'r');
      return key(hs(b, i));
    }
    if (i <= 8) {
      bool room = i == 8;
      char a[10], c[10], e[10];
      fmt1(a, room ? d.roomT : d.caseT, true);
      fmt1(c, room ? d.roomRH : d.caseRH, true);
      fmt1(e, room ? d.roomDP : d.caseDP, true);
      snprintf(b, sizeof(b), "%s\xF8  %s%%  dp %s\xF8", a, c, e);
      if (draw) ftxt(F_LABEL, room ? 198 : 38, 111, C_WHT, C_BLK, b, 118);
      return key(hs(b, i));
    }
    if (i == 9) {   // cooling power meter, 0..1000 W
      uint8_t lit = d.potOk ? clampi(lround(d.potW / 1000 * 15), 0, 15) : 0;
      return slantBar(4, 56, 129, 170, 9, 15, lit, 0, 0, true, draw);
    }
    if (i == 10) {
      const char *v = potText(t, d);
      if (draw) ftxt(F_VALUE, W - 4 - 80, 126, C_WHT, C_BLK, v, 80, 'r');
      return key(hs(v, i));
    }
    if (i == 11) {   // distance to dew: 12 shift lights in the header (1 °C each, first two red) + value
      uint8_t lit = isnan(d.dew) ? 0 : clampi(lround(d.dew), 0, 12);
      signed1(b, d.dew, "\xF8");
      if (draw) {
        slantBar(3, 166, 2, 100, 11, 12, lit, 0, 2, false, true);
        ftxt(F_LABEL, 268, 3, lit < 2 ? C_HOT : C_WHT, C_BLK, b, 36, 'r');
      }
      return key(hs(b, hn(lit, i)));
    }
    if (i == 12) return strip2(d, draw);
    snprintf(b, sizeof(b), "%s%s", d.state, d.sim ? " SIM" : "");   // 13: state in the tab
    if (draw) ftxt(F_LABEL, 46, 3, C_DG, C_WHT, b, 52);
    return key(hs(b, i));
  }

  // ---- mode 3 ----
  switch (i) {
    case 0:
      snprintf(b, sizeof(b), "M3 %s%s", d.state, d.sim ? " SIM" : "");
      if (draw) mono(115, 1, C_WHT, C_MAR, b, 14);
      return key(hs(b, i));
    case 1:
      return heartbeat(W - 3 - 7 * strlen(ver) - 7, 7, C_WHT, C_MAR, draw);
    case 2:
      fmt1(b, d.loop, !isnan(d.loop));
      if (draw) mono(3, 19, C_BLK, C_WHT, b, 4, 'c');
      return key(hs(b, i));
    case 3:
      fmt1(b, d.rad, !isnan(d.rad));
      if (draw) {
        gfx->fillRect(97, 19, 36, 12, C_DG);
        mono(97, 19, C_WHT, C_DG, b, 5, 'c');
      }
      return key(hs(b, i));
    case 4:
      fmt1(b, d.valve, !isnan(d.valve));
      if (draw) {
        gfx->fillRect(58, 97, 36, 12, C_RED);
        mono(58, 97, C_WHT, C_RED, b, 5, 'c');
      }
      return key(hs(b, i));
    case 5: {   // V1: filled white when open
      uint16_t c = d.extOpen ? C_WHT : C_DG;
      if (draw) {
        gfx->fillTriangle(65, 21, 65, 29, 74, 25, c);
        gfx->fillTriangle(87, 21, 87, 29, 78, 25, c);
      }
      return key(c + 11);
    }
    case 6: {   // P2: filled white when running
      uint16_t c = d.p2On ? C_WHT : C_DG;
      if (draw) gfx->fillCircle(48, 25, 5, c);
      return key(c + 13);
    }
    case 7:
      fmt1(t, d.v2deg, d.v2homed);
      snprintf(b, sizeof(b), "%s\xF8%s", t, d.v2homed ? "" : "?");
      if (draw) {
        mono(92, 58, C_DG, C_LG, "V2", 2);
        mono(92, 78, C_MAR, C_LG, b, 6);
      }
      return key(hs(b, i));
    case 8: {
      char a[10], c[10];
      fmt1(a, d.tset, !isnan(d.tset));
      signed1(c, d.valve - d.tset, "");
      snprintf(b, sizeof(b), "Tset %s dev %s", a, c);
      if (draw) mono(2, 114, C_BLK, C_LG, b, 20);
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
        case 16: snprintf(b, sizeof(b), "%X %03X %03X", d.fuses, d.faults, d.alarms); break;   // fuse/fault/alarm
        default: {   // 17 case, 18 room
          bool room = i == 18;
          fmt1(a, room ? d.roomT : d.caseT, !isnan(room ? d.roomT : d.caseT));
          fmt1(c, room ? d.roomRH : d.caseRH, !isnan(room ? d.roomRH : d.caseRH));
          fmt1(e, room ? d.roomDP : d.caseDP, !isnan(room ? d.roomDP : d.caseDP));
          snprintf(b, sizeof(b), "%s\xF8 %s%% %s\xF8", a, c, e);
          break;
        }
      }
      if (draw) mono(W - 3 - 7 * 18, 16 + (i - 9) * 11, (i == 16 && d.faults) ? C_HOT : C_BLK, C_LG, b, 18, 'r');
      return key(hs(b, i));
    }
    case 19:   // chart: new samples since the last draw, then the sweep cursor
      if (draw) {
        if (samples - drawnSamples >= CHART170_W) drawnSamples = samples - CHART170_W;
        while (drawnSamples < samples) chartColumn(drawnSamples++ % CHART170_W);
        gfx->drawFastVLine(5 + samples % CHART170_W, CH_TOP, CH_BOT - CH_TOP + 1, C_DG);
      }
      return key(hn(samples, i));
    default: {   // 20: bottom line (left of the legend), or the fault
      uint8_t n = 0;
      const char *f = faultText(d, n);
      if (d.faults) snprintf(b, sizeof(b), "FAULT %s (%u) 0x%03X", f, n, d.faults);
      else {
        char a[12], c[10];
        signed1(a, d.dew, "\xF8" "C");
        fmt1(c, d.v2deg, d.v2homed);
        snprintf(b, sizeof(b), "TO DEW %s  V2 %s\xF8  %s", a, c, d.no12v ? "NO 12V" : valveText(d));
      }
      if (draw) {
        uint16_t bgc = d.faults ? C_RED : C_LG;
        gfx->fillRect(0, 157, 4, 13, bgc);
        gfx->fillRect(4, 169, 246, 1, bgc);
        ftxt(F_MONO, 4, 157, d.faults ? C_WHT : C_BLK, bgc, b, 246);
      }
      return key(hs(b, i));
    }
  }
}
