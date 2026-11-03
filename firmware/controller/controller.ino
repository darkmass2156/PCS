// PCS outdoor free-cooling controller — see CLAUDE.md (authoritative spec) and CHANGELOG.md.
// Libraries (Library Manager): "Adafruit ST7735 and ST7789 Library", "Adafruit GFX Library".

#include <avr/wdt.h>
#include "version.h"
#include "config.h"
#include "hw.h"
#include "sensors.h"
#include "valve.h"
#include "control.h"
#include "ui.h"
#include "display.h"

// Reset cause, captured before the bootloader/core can touch it.
uint8_t resetFlags __attribute__((section(".noinit")));
void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  resetFlags = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

static uint32_t lastLogAt;

static void printHeader() {
  Serial.print(F("# PCS controller fw " FW_VERSION " logic rev "));
  Serial.print(LOGIC_REV);
  Serial.print(F(" hw " HW_REV " display rev "));
  Serial.print(DISPLAY_REV);
  Serial.print(F(" variant "));
  Serial.println(DISPLAY_VARIANT);
#if DISP_TARGET_ILI9341_SIM
  Serial.println(F("# DISPLAY TARGET: ILI9341 simulator (mirror, BGR, no invert) - not for the real ST7789"));
#endif
#if SIM_ACTIVE
  Serial.print(F("# SIM BUILD - not for the loop:"));
  if (NTC_TRIMPOT_SIM) Serial.print(F(" NTC inputs are trimpots;"));
  if (SIM_FORCE_12V) Serial.print(F(" 12V forced present;"));
  if (SIM_FAKE_P2_TACH) Serial.print(F(" P2 tach faked;"));
  if (SIM_FAKE_FAN_TACH) Serial.print(F(" fan tach faked;"));
  Serial.println();
#endif
  Serial.println(F("ms,mode,state,T1,T2,T3,Tout,Troom,DP_A,DP_B,DP_R,DP,Tset,V2deg,fan%,pump%,"
                   "rpmF1,rpmF2,rpmP2,V1,fuses,faults,alarms"));
}

static void printC(float v, bool ok) {
  Serial.print(',');
  if (ok && !isnan(v)) Serial.print(v, 2);
}

static void logLine(uint32_t now) {
  Serial.print(now);
  Serial.print(',');
  Serial.print((uint8_t)st.mode);
  Serial.print(',');
  Serial.print(stateName(st.state));
  printC(ntcT1.c, ntcT1.valid);
  printC(ntcT2.c, ntcT2.valid);
  printC(ntcT3.c, ntcT3.valid);
  printC(st.tOut, st.tOutOk);
  printC(sht[SHT_R].t, sht[SHT_R].valid);
  printC(sht[SHT_A].dp, sht[SHT_A].valid);
  printC(sht[SHT_B].dp, sht[SHT_B].valid);
  printC(sht[SHT_R].dp, sht[SHT_R].valid);
  printC(st.dp, st.dpValid > 0);
  printC(st.tset, true);
  printC(valveDeg(), valveHomed());
  Serial.print(',');
  Serial.print(hwFanPct());
  Serial.print(',');
  Serial.print(hwPen() ? hwPumpPct() : 0);
  Serial.print(',');
  Serial.print(rpmFan1);
  Serial.print(',');
  Serial.print(rpmFan2);
  Serial.print(',');
  Serial.print(rpmPump);
  Serial.print(',');
  Serial.print(hwSen() ? 1 : 0);
  Serial.print(F(",0x"));
  Serial.print((fusePresent[0] ? 1 : 0) | (fusePresent[1] ? 2 : 0) | (fusePresent[2] ? 4 : 0), HEX);
  Serial.print(F(",0x"));
  Serial.print(st.faults, HEX);
  Serial.print(F(",0x"));
  Serial.println(st.alarms, HEX);
}

// Fault / alarm / state reasons on Serial ('#' lines, the CSV stays parseable): everything active at boot, then
// every change, so the LED colour can be matched to a cause.
static const char FN0[] PROGMEM = "NTC T1 (open/short/out of range)";
static const char FN1[] PROGMEM = "NTC T2 (open/short/out of range)";
static const char FN2[] PROGMEM = "NTC T3 (open/short/out of range)";
static const char FN3[] PROGMEM = "no valid dew point (no SHT)";
static const char FN4[] PROGMEM = "T2 < DP + 1 with P2 running";
static const char FN5[] PROGMEM = "T1 < DP + 2 with P2 running";
static const char FN6[] PROGMEM = "fuse S1 (P2)";
static const char FN7[] PROGMEM = "fuse S3 (V1 + V2)";
static const char FN8[] PROGMEM = "P2 stall (no tach)";
static const char *const FAULT_TXT[] = {FN0, FN1, FN2, FN3, FN4, FN5, FN6, FN7, FN8};
static const char AN0[] PROGMEM = "fuse S2 (fans)";
static const char AN1[] PROGMEM = "fan 1 stall";
static const char AN2[] PROGMEM = "fan 2 stall";
static const char AN3[] PROGMEM = "SHT31 A 0x44 invalid";
static const char AN4[] PROGMEM = "SHT31 B 0x45 invalid";
static const char AN5[] PROGMEM = "SHT30 room invalid";
static const char AN6[] PROGMEM = "DP spread > 2 C";
static const char AN7[] PROGMEM = "Mode 3 with < 2 DP sources (Mode 2 setpoint)";
static const char AN8[] PROGMEM = "T1 < DP + 2 with P2 off";
static const char AN9[] PROGMEM = "feasibility: forced to bypass";
static const char *const ALARM_TXT[] = {AN0, AN1, AN2, AN3, AN4, AN5, AN6, AN7, AN8, AN9};

static void printBits(const __FlashStringHelper *kind, uint16_t on, uint16_t off, const char *const *txt, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    uint16_t m = 1 << i;
    if (!((on | off) & m)) continue;
    Serial.print(F("# "));
    Serial.print(kind);
    Serial.print(on & m ? F(" ON:  ") : F(" off: "));
    Serial.println((const __FlashStringHelper *)txt[i]);
  }
}

static void reportEvents(bool all) {
  static uint16_t faults, alarms;
  static BranchState state = (BranchState)0xFF;
  static Mode mode;
  if (all) {
    faults = alarms = 0;
    state = (BranchState)0xFF;
  }
  if (st.state != state || st.mode != mode) {
    Serial.print(F("# state "));
    Serial.print(stateName(st.state));
    Serial.print(F(", mode "));
    Serial.println((uint8_t)st.mode);
    state = st.state;
    mode = st.mode;
  }
  printBits(F("FAULT"), st.faults & ~faults, faults & ~st.faults, FAULT_TXT, 9);
  printBits(F("ALARM"), st.alarms & ~alarms, alarms & ~st.alarms, ALARM_TXT, 10);
  if (all && !st.faults && !st.alarms) Serial.println(F("# no faults, no alarms"));
  faults = st.faults;
  alarms = st.alarms;
}

static void printStatus() {
  printHeader();
  Serial.print(F("# reset flags 0x"));
  Serial.println(resetFlags, HEX);
  Serial.print(F("# Tset margin ")); Serial.print(MARGIN_DP_C);
  Serial.print(F(" delta ")); Serial.print(DELTA_ROOM_C);
  Serial.print(F(" P2 ")); Serial.print(P2_PWM_RUN);
  Serial.print(F("% fanmin ")); Serial.print(FAN_PWM_MIN);
  Serial.print(F("% V2 open/close ms ")); Serial.print(V2_T_OPEN_MS);
  Serial.print('/'); Serial.print(V2_T_CLOSE_MS);
  Serial.print(F(" backlash ")); Serial.print(V2_BACKLASH_MS);
  Serial.print(F(" beta ")); Serial.println(NTC_BETA);
  Serial.print(F("# fuse adc ")); Serial.print(fuseAdc[0]);
  Serial.print(' '); Serial.print(fuseAdc[1]);
  Serial.print(' '); Serial.print(fuseAdc[2]);
  Serial.print(F(" faults live 0x")); Serial.print(st.faultsLive, HEX);
  Serial.print(F(" SHT errs ")); Serial.print(sht[SHT_A].errCount);
  Serial.print(' '); Serial.print(sht[SHT_B].errCount);
  Serial.print(' '); Serial.print(sht[SHT_R].errCount);
  Serial.print(F(" I2C recoveries ")); Serial.println(i2cRecoveries);
  // NTC inputs as the ADC sees them: counts, resistance (10k pull-up), temperature
  const NtcReading *n[3] = {&ntcT1, &ntcT2, &ntcT3};
  const char *const NAME[3] = {"T1 A5", "T2 A4", "T3 A3"};
  for (uint8_t i = 0; i < 3; i++) {
    Serial.print(F("# NTC ")); Serial.print(NAME[i]);
    Serial.print(F(": adc ")); Serial.print(n[i]->adc, 1);
    Serial.print(F(" = ")); Serial.print(n[i]->adc < 1022.5f ? NTC_R_PULLUP * n[i]->adc / (1023.0f - n[i]->adc) : 1e9f, 0);
    Serial.print(F(" ohm -> ")); Serial.print(n[i]->c, 2);
    Serial.print(n[i]->valid ? F(" C") : F(" C (invalid)"));
    Serial.println(n[i]->floating ? F("  PIN FLOATING: no connection from the LG node to the Mega pin") : F(""));
  }
}

// ---------------- serial commands ----------------
static void testHelp() {
  Serial.println(F("# commands: ? status | h help | t test mode on (Mode 1, idle) | x test mode off"));
  Serial.println(F("#   in test mode: f<0-100> fans % | p<0-100> P2 % (p0 = PEN off) | v1 / v0 V1 open / closed"));
  Serial.println(F("#                 o<ms> / c<ms> V2 open / close run (max 15000) | k park V2 | s stop V2"));
  Serial.println(F("#   faults stay armed; test mode ends on x, a button, a fault, 12V loss or 30 min idle"));
}

static void testState() {
  Serial.print(F("# TEST fans ")); Serial.print(hwFanPct());
  Serial.print(F("% P2 ")); Serial.print(hwPen() ? F("on ") : F("off ")); Serial.print(hwPumpPct());
  Serial.print(F("% V1 ")); Serial.print(hwSen() ? F("open") : F("closed"));
  Serial.print(F(" V2 ")); Serial.print(valveDeg(), 1); Serial.println(valveHomed() ? F(" deg") : F(" deg (not homed)"));
}

static void runCommand(char *s) {
  char c = s[0];
  long n = atol(s + 1);
  if (c == '?') {
    printStatus();
    sensorsRoomDiag();
    reportEvents(true);
    return;
  }
  if (c == 'h') { testHelp(); return; }
  if (c == 't') {
    if (controlTestEnter()) { Serial.println(F("# TEST mode ON")); testState(); }
    else Serial.println(F("# TEST refused: needs Mode 1 (BTN1), idle, 12V present and no fault"));
    return;
  }
  if (c == 'x') {
    if (controlTesting()) { controlTestExit(); Serial.println(F("# TEST mode OFF, V2 parking")); }
    return;
  }
  if (!strchr("fpvocks", c)) { Serial.println(F("# ? unknown command, h for help")); return; }
  if (!controlTesting()) { Serial.println(F("# not in test mode: send t first (Mode 1)")); return; }
  bool ok = true;
  switch (c) {
    case 'f': controlTestFan(constrain(n, 0, 100)); break;
    case 'p': controlTestPump(constrain(n, 0, 100)); break;
    case 'v': controlTestV1(n == 1); break;
    case 'o': ok = valveJog(V_OPEN, constrain(n, 1, 15000)); break;
    case 'c': ok = valveJog(V_CLOSE, constrain(n, 1, 15000)); break;
    case 'k': valvePark(); break;
    case 's': valveStop(); break;
  }
  if (!ok) Serial.println(F("# V2 busy or no 12V on S3: not moved"));
  testState();
}

// Line based: a command ends with CR/LF or 50 ms of silence (works with any serial-monitor line ending).
static void serialInput(uint32_t now) {
  static char buf[16];
  static uint8_t len;
  static uint32_t lastCharAt;
  while (Serial.available()) {
    char ch = Serial.read();
    lastCharAt = now;
    if (ch == '\r' || ch == '\n') {
      if (len) { buf[len] = 0; runCommand(buf); len = 0; }
    } else if (len < sizeof(buf) - 1 && ch != ' ') {
      buf[len++] = ch;
    }
  }
  if (len && now - lastCharAt >= 50) { buf[len] = 0; runCommand(buf); len = 0; }
}

void setup() {
  hwAllOutputsLow();               // safety rule 1: before anything else
  valveInit();
  hwInit();

  Serial.begin(115200);
  printHeader();
  Serial.print(F("# reset flags 0x"));
  Serial.println(resetFlags, HEX);
  if (resetFlags & _BV(WDRF)) Serial.println(F("# WARNING: watchdog reset"));

  sensorsInit();
  uiInit();
  controlInit(millis());
  displayInit();                   // blocking; done before the watchdog starts

#if WDT_ENABLE
  wdt_enable(WDTO_2S);
#endif
}

void loop() {
  uint32_t now = millis();
#if WDT_ENABLE
  wdt_reset();
#endif
  hwUpdateFuses(now);
  hwUpdateTach(now);
  sensorsUpdate(now);
  uiUpdate(now);
  controlUpdate(now);
  valveUpdate(now);

  if (now - lastLogAt >= 1000) {
    lastLogAt = now;
    logLine(now);
  }
  static bool listed;              // state changes from boot; at the end of the boot grace (sensor faults are
  if (!listed && now >= BOOT_GRACE_MS) {   // evaluated from then on) everything active is listed once
    reportEvents(true);
    listed = true;
  } else reportEvents(false);
  serialInput(now);

  displayUpdate(now);              // last: never in the safety path
}
