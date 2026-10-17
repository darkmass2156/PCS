#include "display.h"
#include "config.h"
#include "control.h"
#include "sensors.h"
#include "valve.h"
#include "hw.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <EEPROM.h>
#include <math.h>

#include "layout.h"
#include "splash.h"

static const uint16_t W = LAYOUT_W, H = LAYOUT_H;

static Adafruit_ST7789 tft(PIN_DISP_CS, PIN_DISP_DC, -1);

static const uint16_t BG = ST77XX_BLACK;
static const uint16_t C_LABEL = 0x8410;   // grey
static const uint16_t C_VALUE = ST77XX_WHITE;
static const uint16_t C_FAULT = ST77XX_RED;
static const uint16_t C_ALARM = ST77XX_YELLOW;

enum Field : uint8_t {
  FD_MODE, FD_HEART, FD_T1, FD_T2, FD_TSET, FD_TOUT, FD_POT, FD_ROOM, FD_DP,
  FD_P2, FD_V1, FD_V2, FD_MSG, FD_COUNT
};

struct FieldDef {
  uint8_t row;
  uint16_t labelX;
  uint16_t valueX;
  uint8_t width;      // chars, padded so stale text is overwritten
};

#if NTC_TRIMPOT_SIM
static const uint8_t HB_W = 9;    // "SIM mm:ss"
#else
static const uint8_t HB_W = 8;    // "hh:mm:ss"
#endif
static const FieldDef fields[FD_COUNT] = LAYOUT_FIELDS;
static const char *const labels[FD_COUNT] = {
  NULL, NULL, "T1", "T2", "Tset", "Tout", "dTpot", "Room", "DP", "P2", "V1", "V2", NULL};
static BootSplash splash;

static char val[FD_COUNT][21];
static uint16_t color[FD_COUNT];
static uint16_t dirty;
static uint8_t nextField;
static uint32_t lastValuesAt, lastReinitAt;

// ---------------- init sequence (2 MHz) ----------------
// SWRESET keeps GRAM, so the periodic re-init doesn't blank the screen.
struct InitStep { uint8_t cmd; uint8_t arg; bool hasArg; uint8_t waitMs; };
static const InitStep initSeq[] = {
  {ST77XX_SWRESET, 0, false, 150},
  {ST77XX_SLPOUT, 0, false, 120},
  {ST77XX_COLMOD, 0x55, true, 10},
  {ST77XX_NORON, 0, false, 10},
  {ST77XX_DISPON, 0, false, 10},
};
static const uint8_t INIT_STEPS = sizeof(initSeq) / sizeof(initSeq[0]);
static int8_t initStep = -1;      // -1 = not re-initialising
static uint32_t initWaitUntil;

// MADCTL bits
static const uint8_t MADCTL_MY  = 0x80;
static const uint8_t MADCTL_MX  = 0x40;
static const uint8_t MADCTL_BGR = 0x08;

// Called at boot AND after every periodic re-init, so orientation/mirror always stays consistent.
// (Mirror support by Vlad.)
static void applyOrientation() {
  tft.setRotation(DISP_ROTATION);   // sets width/height and panel offsets for init(W, H)

  // Same base values Adafruit_ST7789::setRotation() uses for rotation 0..3
  static const uint8_t madctl[4] = {0xC0, 0xA0, 0x00, 0x60};
  uint8_t r = DISP_ROTATION & 3;
  uint8_t m = madctl[r];

  // Horizontal mirror. In landscape (r = 1/3, MV set) the screen X axis is driven by MY,
  // in portrait (r = 0/2) by MX.
  if (DISP_MIRROR_X) m ^= (r & 1) ? MADCTL_MY : MADCTL_MX;

  if (DISP_BGR) m |= MADCTL_BGR;
  tft.sendCommand(ST77XX_MADCTL, &m, 1);
  tft.invertDisplay(DISP_INVERT);
}

static void sendStep(uint8_t i) {
  uint8_t a = initSeq[i].arg;
  if (initSeq[i].hasArg) tft.sendCommand(initSeq[i].cmd, &a, 1);
  else tft.sendCommand(initSeq[i].cmd);
}

// Random splash per boot, never the same one twice in a row. Kept in EEPROM (the mode uses 0/1):
// a boot counter that varies the seed (micros() and ADC noise are nearly identical every boot) and the
// last penguin shown. One EEPROM write per boot is ~27 years of 10 boots/day.
static const uint8_t EE_SPLASH_COUNT = 2, EE_SPLASH_LAST = 3;

static uint8_t pickSplash() {
  uint8_t boots = EEPROM.read(EE_SPLASH_COUNT) + 1;
  EEPROM.update(EE_SPLASH_COUNT, boots);
  uint32_t seed = micros() ^ ((uint32_t)boots * 2654435761UL);
  for (uint8_t i = 0; i < 16; i++) seed = (seed << 1) ^ (analogRead(PIN_T3 + (i % 3)) & 1);  // ADC LSB noise
  randomSeed(seed);
  uint8_t last = EEPROM.read(EE_SPLASH_LAST);   // 0xFF on a blank EEPROM = none
  uint8_t n = random(SPLASH_COUNT);
  if (last < SPLASH_COUNT && n == last) n = (n + 1 + random(SPLASH_COUNT - 1)) % SPLASH_COUNT;
  EEPROM.update(EE_SPLASH_LAST, n);
  return n;
}

void displayInit() {
  tft.init(LAYOUT_INIT_W, LAYOUT_INIT_H);   // native portrait size, at the library default clock ...
  tft.setSPISpeed(DISP_SPI_HZ);   // ... so redo it at 2 MHz for the long cable
  for (uint8_t i = 0; i < INIT_STEPS; i++) {
    sendStep(i);
    delay(initSeq[i].waitMs);
  }
  applyOrientation();               // DISP_ROTATION 1/3 -> landscape W x H
  tft.fillScreen(BG);
  tft.cp437(true);
  dirty = (1 << FD_COUNT) - 1;
  lastReinitAt = millis();
#if BOOT_SPLASH
  splash.begin(&tft, W, H, pickSplash(), lastReinitAt);
#endif
}

// ---------------- values ----------------
static void fmtC(char *out, float v, bool ok) {
  if (!ok || isnan(v)) strcpy(out, "--");
  else dtostrf(v, 0, 1, out);
}

static uint8_t bitCount(uint16_t m) {
  uint8_t n = 0;
  for (; m; m &= m - 1) n++;
  return n;
}

static const char *const faultNames[] = {
  "NTC T1", "NTC T2", "NTC T3", "NO DP", "T2 LOW", "T1 LOW", "FUSE P2", "FUSE V", "P2 STALL"};
static const char *const alarmNames[] = {
  "FUSE FAN", "FAN1 STALL", "FAN2 STALL", "SHT A", "SHT B", "SHT ROOM", "DP SPREAD",
  "M3 DEGRADED", "T1 LOW", "FEAS WAIT"};

// n-th set bit of mask -> name
static const char *nthName(uint16_t mask, uint8_t n, const char *const *names) {
  for (uint8_t i = 0; i < 16; i++) {
    if (!(mask & (1 << i))) continue;
    if (n-- == 0) return names[i];
  }
  return "";
}

static void setVal(uint8_t f, const char *s, uint16_t c) {
  if (strcmp(val[f], s) != 0 || color[f] != c) {
    strncpy(val[f], s, sizeof(val[f]) - 1);
    val[f][sizeof(val[f]) - 1] = 0;
    color[f] = c;
    dirty |= 1 << f;
  }
}

static void computeValues(uint32_t now) {
  char b[24], t[12];

  // Mode / state
  if (st.state == ST_NO12V) {
    setVal(FD_MODE, "NO 12V", C_ALARM);
  } else {
    const char *s = st.state == ST_IDLE ? (st.mode == MODE_OFF ? "OFF" : "SLEEP") : stateName(st.state);
    snprintf(b, sizeof(b), "M%u %s", st.mode, s);
    uint16_t c = st.state == ST_SAFE ? C_FAULT
               : st.mode == MODE_LOADCAP ? ST77XX_GREEN
               : st.mode == MODE_MAX ? ST77XX_CYAN : ST77XX_ORANGE;
    setVal(FD_MODE, b, c);
  }

  // Heartbeat: uptime
  uint32_t s = now / 1000;
#if NTC_TRIMPOT_SIM
  snprintf(b, sizeof(b), "SIM %02u:%02u", (unsigned)((s / 60) % 60), (unsigned)(s % 60));
#else
  snprintf(b, sizeof(b), "%02u:%02u:%02u", (unsigned)((s / 3600) % 100), (unsigned)((s / 60) % 60),
           (unsigned)(s % 60));
#endif
  setVal(FD_HEART, b, C_LABEL);

  fmtC(b, ntcT1.c, ntcT1.valid);  setVal(FD_T1, b, ntcT1.valid ? C_VALUE : C_FAULT);
  fmtC(b, ntcT2.c, ntcT2.valid);  setVal(FD_T2, b, ntcT2.valid ? C_VALUE : C_FAULT);
  fmtC(b, st.tset, true);         setVal(FD_TSET, b, C_VALUE);
  fmtC(b, st.tOut, st.tOutOk);    setVal(FD_TOUT, b, C_VALUE);

  if (st.tOutOk && ntcT1.valid) {
    float pot = ntcT1.c - st.tOut;
    fmtC(t, pot, true);
    if (K_POT > 0) snprintf(b, sizeof(b), "%d %ldW", (int)lround(pot), (long)(K_POT * pot));  // fits 8 chars
    else strcpy(b, t);
  } else {
    strcpy(b, "--");
  }
  setVal(FD_POT, b, C_VALUE);

  fmtC(b, sht[SHT_R].t, sht[SHT_R].valid);  setVal(FD_ROOM, b, C_VALUE);

  fmtC(t, st.dp, st.dpValid > 0);
  snprintf(b, sizeof(b), "%s x%u", t, st.dpValid);
  setVal(FD_DP, b, st.dpValid ? C_VALUE : C_FAULT);

  if (hwPen()) snprintf(b, sizeof(b), "%u%% %u", hwPumpPct(), rpmPump);
  else strcpy(b, "OFF");
  setVal(FD_P2, b, C_VALUE);

  setVal(FD_V1, hwSen() ? "OPEN" : "SHUT", C_VALUE);

  dtostrf(valveDeg(), 0, 1, t);
  ValveDir m = valveMoving();
  snprintf(b, sizeof(b), "%s%c%s%s", t, (char)248, valveHomed() ? "" : "?",
           m == V_OPEN ? " >" : m == V_CLOSE ? " <" : "");
  setVal(FD_V2, b, C_VALUE);

  // Message line: rotate through faults (red), then alarms (yellow), 2 s each.
  uint8_t nf = bitCount(st.faults), na = bitCount(st.alarms);
  if (nf + na == 0) {
    setVal(FD_MSG, "", C_VALUE);
  } else {
    uint8_t i = (s / 2) % (nf + na);
    if (i < nf) setVal(FD_MSG, nthName(st.faults, i, faultNames), C_FAULT);
    else setVal(FD_MSG, nthName(st.alarms, i - nf, alarmNames), C_ALARM);
  }
}

static void drawField(uint8_t f) {
  const FieldDef &d = fields[f];
  int16_t y = LAYOUT_TOP + d.row * LAYOUT_ROW_H;
  uint8_t size = f == FD_HEART ? 1 : LAYOUT_VALUE_SIZE;
  uint8_t width = f == FD_HEART ? HB_W : d.width;
  int16_t x = f == FD_HEART ? W - 4 - 6 * HB_W : d.valueX;
  if (labels[f]) {
    tft.setTextSize(1);
    tft.setTextColor(C_LABEL, BG);
    tft.setCursor(d.labelX, y + LAYOUT_LABEL_DY);
    tft.print(labels[f]);
  }
  char b[32];
  if (width > sizeof(b) - 1) width = sizeof(b) - 1;
  snprintf(b, sizeof(b), "%-*s", width, val[f]);
  b[width] = 0;
  tft.setTextSize(size);
  tft.setTextColor(color[f], BG);
  tft.setCursor(x, y);
  tft.print(b);
}

void displayUpdate(uint32_t now) {
  // Boot splash first, one small step per call; then the dashboard from a black screen.
  if (splash.active()) {
    if (splash.update(now)) {
      dirty = (1 << FD_COUNT) - 1;
      lastValuesAt = now - 1000;
      lastReinitAt = now;
    }
    return;
  }
  // Non-blocking re-init, one command per call.
  if (initStep >= 0) {
    if ((int32_t)(now - initWaitUntil) < 0) return;
    if (initStep < INIT_STEPS) {
      sendStep(initStep);
      initWaitUntil = now + initSeq[initStep].waitMs;
      initStep++;
    } else {
      applyOrientation();
      initStep = -1;
      dirty = (1 << FD_COUNT) - 1;   // redraw everything (labels included), no clear
      lastReinitAt = now;
    }
    return;
  }
  if (now - lastReinitAt >= DISP_REINIT_MS) {
    initStep = 0;
    initWaitUntil = now;
    return;
  }

  if (now - lastValuesAt >= 1000) {
    lastValuesAt = now;
    computeValues(now);
  }

  // One field per call keeps each SPI burst short.
  for (uint8_t k = 0; k < FD_COUNT; k++) {
    uint8_t f = (nextField + k) % FD_COUNT;
    if (dirty & (1 << f)) {
      dirty &= ~(1 << f);
      drawField(f);
      nextField = f + 1;
      return;
    }
  }
}
