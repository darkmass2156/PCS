#include "display.h"
#include "config.h"
#include "version.h"
#include "control.h"
#include "sensors.h"
#include "valve.h"
#include "hw.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <EEPROM.h>
#include <math.h>

#include "splash.h"
#include "screens.h"

// Display rev 3: the dashboard screens are drawn for 280x240 (the 1.69" MSP1691 in landscape).
#if DISPLAY_VARIANT != 1
#error "Display rev 3 screens are 280x240 only (DISPLAY_VARIANT 1). The 1.9\" variant needs its own layouts."
#endif
static const uint16_t INIT_W = 240, INIT_H = 280;   // native portrait size for init()
static const uint16_t W = 280, H = 240;              // after DISP_ROTATION 1/3

static Adafruit_ST7789 tft(PIN_DISP_CS, PIN_DISP_DC, -1);
static BootSplash splash;
static Screens screens;
static UiData ui;
static bool uiReady;
static uint32_t lastValuesAt, lastReinitAt;

// ---------------- init sequence (at DISP_SPI_HZ) ----------------
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
  tft.init(INIT_W, INIT_H);         // runs at the library default clock ...
  tft.setSPISpeed(DISP_SPI_HZ);     // ... so redo it at DISP_SPI_HZ (4 MHz) for the long cable
  for (uint8_t i = 0; i < INIT_STEPS; i++) {
    sendStep(i);
    delay(initSeq[i].waitMs);
  }
  applyOrientation();               // DISP_ROTATION 1/3 -> landscape W x H
  tft.fillScreen(ST77XX_BLACK);
  tft.cp437(true);
  screens.begin(&tft, W, H, "v" FW_VERSION);
  lastReinitAt = millis();
#if BOOT_SPLASH
  splash.begin(&tft, W, H, pickSplash(), FW_VERSION, DISP_INVERT, lastReinitAt);
#endif
}

// ---------------- data for the screens ----------------
static float maxValid(float a, bool aOk, float b, bool bOk) {
  if (aOk && bOk) return a > b ? a : b;
  return aOk ? a : bOk ? b : NAN;
}

static void buildUi() {
  ui.mode = st.mode;
  ui.state = st.state == ST_IDLE ? (st.mode == MODE_OFF ? "OFF" : "SLEEP") : stateName(st.state);
  ui.no12v = st.state == ST_NO12V;
  ui.sim = NTC_TRIMPOT_SIM;
  ui.rad = ntcT3.valid ? ntcT3.c : NAN;
  ui.loop = ntcT1.valid ? ntcT1.c : NAN;
  ui.valve = ntcT2.valid ? ntcT2.c : NAN;
  const ShtReading &a = sht[SHT_A], &b = sht[SHT_B], &r = sht[SHT_R];
  ui.caseT = maxValid(a.t, a.valid, b.t, b.valid);          // the highest of the two, per value
  ui.caseRH = maxValid(a.rh, a.valid, b.rh, b.valid);
  ui.caseDP = maxValid(a.dp, a.valid, b.dp, b.valid);
  ui.roomT = r.valid ? r.t : NAN;
  ui.roomRH = r.valid ? r.rh : NAN;
  ui.roomDP = r.valid ? r.dp : NAN;
  ui.dpA = a.valid ? a.dp : NAN;
  ui.dpB = b.valid ? b.dp : NAN;
  ui.dpR = r.valid ? r.dp : NAN;
  ui.dpValid = st.dpValid;
  ui.dp = st.dpValid ? st.dp : NAN;
  ui.tset = st.tset;
  ui.tout = st.tOut;
  ui.toutOk = st.tOutOk;
  ui.potOk = st.tOutOk && ntcT1.valid;
  ui.potK = ui.potOk ? ntcT1.c - st.tOut : NAN;
  ui.potW = ui.potOk ? K_POT * ui.potK : NAN;
  ui.extOpen = hwSen();
  ui.homing = st.state == ST_HOMING || (!valveHomed() && valveBusy());
  ui.mixing = valveHomed() && valveDeg() > V2_FLOW_DEG;
  ui.v2deg = valveDeg();
  ui.v2homed = valveHomed();
  ui.fanPct = hwFanPct();
  ui.rpmF1 = rpmFan1;
  ui.rpmF2 = rpmFan2;
  ui.p2On = hwPen();
  ui.p2Pct = hwPumpPct();
  ui.rpmP2 = rpmPump;
  ui.dew = ui.valve - ui.dp;                              // NAN if either is
  ui.faults = st.faults;
  ui.alarms = st.alarms;
  ui.fuses = (fusePresent[0] ? 1 : 0) | (fusePresent[1] ? 2 : 0) | (fusePresent[2] ? 4 : 0);
}

void displayUpdate(uint32_t now) {
  // Boot sequence first (Pingu, glitch, ROG), one small step per call.
  if (splash.active()) {
    if (splash.update(now)) {
      lastReinitAt = now;
      lastValuesAt = now - 1000;
    }
    return;
  }
  // Non-blocking re-init, one command per call. SWRESET keeps GRAM, so nothing needs redrawing.
  if (initStep >= 0) {
    if ((int32_t)(now - initWaitUntil) < 0) return;
    if (initStep < INIT_STEPS) {
      sendStep(initStep);
      initWaitUntil = now + initSeq[initStep].waitMs;
      initStep++;
    } else {
      applyOrientation();
      initStep = -1;
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
    buildUi();
    screens.sample(ui);
    uiReady = true;
  }
  if (!uiReady) return;
  ui.mode = st.mode;                // mode switches show at once, not on the next 1 s tick
  screens.update(now, ui);
}
