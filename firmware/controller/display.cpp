#include "display.h"
#include "config.h"
#include "control.h"
#include "sensors.h"
#include "valve.h"
#include "hw.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <math.h>

#include "layout.h"

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
  uint8_t x;
  uint8_t size;
  uint8_t width;      // chars, padded so stale text is overwritten
  const char *label;  // NULL = no label
};

#if NTC_TRIMPOT_SIM
static const uint8_t HB_W = 9;    // "SIM mm:ss"
#else
static const uint8_t HB_W = 8;    // "hh:mm:ss"
#endif
static const uint8_t VX = LAYOUT_VALUE_X, VS = LAYOUT_VALUE_SIZE, VW = LAYOUT_VALUE_W;
static const FieldDef fields[FD_COUNT] = {
  {LAYOUT_LABEL_X, VS, 9, NULL},
  {(uint8_t)(W - 4 - 6 * HB_W), 1, HB_W, NULL},
  {VX, VS, VW, "T1"},
  {VX, VS, VW, "T2"},
  {VX, VS, VW, "Tset"},
  {VX, VS, VW, "Tout"},
  {VX, VS, VW, "dTpot"},
  {VX, VS, VW, "Room"},
  {VX, VS, VW, "DP"},
  {VX, VS, VW, "P2"},
  {VX, VS, VW, "V1"},
  {VX, VS, VW, "V2"},
  {LAYOUT_LABEL_X, VS, (uint8_t)((W - 2 * LAYOUT_LABEL_X) / (6 * VS)), NULL},
};
static const uint8_t rows[FD_COUNT] = LAYOUT_ROWS;

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

static void applyOrientation() {
  tft.setRotation(DISP_ROTATION);   // also sets the panel offsets for init(W, H)
  if (DISP_BGR) {
    static const uint8_t madctl[4] = {0xC0, 0xA0, 0x00, 0x60};
    uint8_t m = madctl[DISP_ROTATION & 3] | 0x08;
    tft.sendCommand(ST77XX_MADCTL, &m, 1);
  }
  tft.invertDisplay(DISP_INVERT);
}

static void sendStep(uint8_t i) {
  uint8_t a = initSeq[i].arg;
  if (initSeq[i].hasArg) tft.sendCommand(initSeq[i].cmd, &a, 1);
  else tft.sendCommand(initSeq[i].cmd);
}

void displayInit() {
  tft.init(W, H);                 // runs at the library default clock ...
  tft.setSPISpeed(DISP_SPI_HZ);   // ... so redo it at 2 MHz for the long cable
  for (uint8_t i = 0; i < INIT_STEPS; i++) {
    sendStep(i);
    delay(initSeq[i].waitMs);
  }
  applyOrientation();
  tft.fillScreen(BG);
  tft.cp437(true);
  dirty = (1 << FD_COUNT) - 1;
  lastReinitAt = millis();
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
    if (K_POT > 0) snprintf(b, sizeof(b), "%s %ldW", t, (long)(K_POT * pot));
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
  int16_t y = LAYOUT_TOP + rows[f] * LAYOUT_ROW_H;
  if (d.label) {
    tft.setTextSize(1);
    tft.setTextColor(C_LABEL, BG);
    tft.setCursor(LAYOUT_LABEL_X, y + LAYOUT_LABEL_DY);
    tft.print(d.label);
  }
  char b[24];
  snprintf(b, sizeof(b), "%-*s", d.width, val[f]);
  b[d.width] = 0;
  tft.setTextSize(d.size);
  tft.setTextColor(color[f], BG);
  tft.setCursor(d.x, y);
  tft.print(b);
}

void displayUpdate(uint32_t now) {
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
