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
#include <EEPROM.h>

static void printResets();
static void printSim();

// Reset cause, captured before the bootloader/core can touch it.
uint8_t resetFlags __attribute__((section(".noinit")));
void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  resetFlags = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

static uint32_t lastLogAt;
static bool csvMode;   // false: readable status line (default), true: CSV. EEPROM 8 (1 = CSV)
static const uint8_t EE_LOGFMT = 8;

static void printHeader() {
  Serial.print(F("# PCS controller fw " FW_VERSION " logic rev "));
  Serial.print(LOGIC_REV);
  Serial.print(F(" hw " HW_REV " display rev "));
  Serial.println(DISPLAY_REV);
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
  if (csvMode) {
    Serial.println(F("ms,mode,state,T1,T2,T3,Tout,Troom,DP_A,DP_B,DP_R,DP,Tset,V2deg,V2mm,V2out%,fan%,pump%,"
                     "rpmF1,rpmF2,rpmP2,V1,fuses,faults,alarms"));
  } else {
    Serial.println(F("# 1 Hz status (send csv for the machine-readable log). Temperatures in C."));
    Serial.println(F("#  mode state | Ret = loop return T1 / Mix = V2 outlet T2 / Rad = outdoor rad T3 / Tout | dew point DP, setpoint Tset,"));
    Serial.println(F("#  case + room | V2 angle, port opening mm, outdoor share % | fans % rpm | P2 % rpm | faults/alarms"));
  }
}

static void printC(float v, bool ok) {
  Serial.print(',');
  if (ok && !isnan(v)) Serial.print(v, 2);
}

// ---- readable 1 Hz status line ----
static void pad(const char *s, uint8_t w) {
  Serial.print(s);
  for (uint8_t n = strlen(s); n < w; n++) Serial.print(' ');
}
static void num(float v, bool ok, uint8_t dec, uint8_t w) {   // right-aligned, "--" when invalid
  char b[12];
  if (ok && !isnan(v)) dtostrf(v, w, dec, b);
  else { uint8_t i = 0; for (; i + 2 < w; i++) b[i] = ' '; b[i++] = '-'; b[i++] = '-'; b[i] = 0; }
  Serial.print(b);
}
static void numU(long v, uint8_t w) {
  char b[12];
  ltoa(v, b, 10);
  for (uint8_t n = strlen(b); n < w; n++) Serial.print(' ');
  Serial.print(b);
}
static const char *const F_SHORT[] = {"NTC-RET", "NTC-MIX", "NTC-RAD", "NO-DP", "MIX-LOW", "RET-LOW", "FUSE-P2", "FUSE-V", "P2-STALL"};
static const char *const A_SHORT[] = {"FUSE-FAN", "FAN1-STALL", "FAN2-STALL", "SHT-A", "SHT-B", "SHT-R", "DP-SPREAD",
                                      "M3-DEGRADED", "RET-LOW-IDLE", "FEAS-FORCED"};

static void statusLine(uint32_t now) {
  Serial.print('M'); Serial.print((uint8_t)st.mode); Serial.print(' ');
  const char *sn = st.state == ST_IDLE ? (st.mode == MODE_OFF ? "OFF" : "SLEEP") : stateName(st.state);
  pad(sn, 6);
  Serial.print(F("| Ret")); num(ntcT1.c, ntcT1.valid, 1, 6);
  Serial.print(F("  Mix")); num(ntcT2.c, ntcT2.valid, 1, 6);
  Serial.print(F("  Rad")); num(ntcT3.c, ntcT3.valid, 1, 6);
  Serial.print(F("  Tout")); num(st.tOut, st.tOutOk, 1, 6);
  Serial.print(F(" | DP")); num(st.dp, st.dpValid > 0, 1, 6);
  Serial.print(F("  Tset")); num(st.tset, true, 1, 6);
  float caseT = sht[SHT_A].valid ? sht[SHT_A].t : NAN, caseRH = sht[SHT_A].valid ? sht[SHT_A].rh : NAN;
  if (sht[SHT_B].valid && (isnan(caseT) || sht[SHT_B].t > caseT)) caseT = sht[SHT_B].t;
  if (sht[SHT_B].valid && (isnan(caseRH) || sht[SHT_B].rh > caseRH)) caseRH = sht[SHT_B].rh;
  Serial.print(F("  case")); num(caseT, true, 1, 5); Serial.print('/'); num(caseRH, true, 0, 3); Serial.print('%');
  Serial.print(F("  room")); num(sht[SHT_R].t, sht[SHT_R].valid, 1, 5); Serial.print('/');
  num(sht[SHT_R].rh, sht[SHT_R].valid, 0, 3); Serial.print('%');
  Serial.print(F(" | V2")); num(valveDeg(), valveHomed(), 1, 5); Serial.print(F(" deg"));
  num(valveOpenMm(), valveHomed(), 1, 5); Serial.print(F(" mm"));
  num(valveOutdoorPct(), valveHomed(), 0, 4); Serial.print(F("% out"));
  if (valveBusy()) Serial.print(valveMoving() == V_OPEN ? F(" >") : valveMoving() == V_CLOSE ? F(" <") : F(" ."));
  else Serial.print(F("  "));
  Serial.print(F(" | fan")); numU(hwFanPct(), 4); Serial.print('%');
  if (FAN1_TACH_FITTED) { numU(rpmFan1, 5); Serial.print('/'); } else Serial.print(F("  rpm"));
  numU(rpmFan2, 5);
  Serial.print(F(" | P2")); numU(hwPen() ? hwPumpPct() : 0, 4); Serial.print('%'); numU(rpmPump, 5);
  if (V1_FITTED) { Serial.print(F(" | V1 ")); Serial.print(hwSen() ? F("open") : F("shut")); }
  if (!hw12vPresent()) Serial.print(F(" | NO 12V"));
  if (controlSimActive()) Serial.print(F(" | SIM"));
  Serial.print(F(" |"));
  if (!st.faults && !st.alarms) Serial.print(F(" ok"));
  for (uint8_t i = 0; i < 9; i++) if (st.faults & (1 << i)) { Serial.print(F(" F:")); Serial.print(F_SHORT[i]); }
  for (uint8_t i = 0; i < 10; i++) if (st.alarms & (1 << i)) { Serial.print(F(" A:")); Serial.print(A_SHORT[i]); }
  Serial.println();
  (void)now;
}

static void logLine(uint32_t now) {
  if (!csvMode) { statusLine(now); return; }
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
  printC(valveOpenMm(), valveHomed());
  printC(valveOutdoorPct(), valveHomed());
  Serial.print(',');
  Serial.print(hwFanPct());
  Serial.print(',');
  Serial.print(hwPen() ? hwPumpPct() : 0);
  Serial.print(',');
  if (FAN1_TACH_FITTED) Serial.print(rpmFan1);   // empty field: no fan 1 tach
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
static const char FN0[] PROGMEM = "NTC Ret T1 loop return (open/short/out of range)";
static const char FN1[] PROGMEM = "NTC Mix T2 V2 outlet (open/short/out of range)";
static const char FN2[] PROGMEM = "NTC Rad T3 outdoor rad (open/short/out of range)";
static const char FN3[] PROGMEM = "no valid dew point (no SHT)";
static const char FN4[] PROGMEM = "Mix T2 < DP + 1 with P2 running";
static const char FN5[] PROGMEM = "Ret T1 < DP + 2 with P2 running";
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
static const char AN8[] PROGMEM = "Ret T1 < DP + 2 with P2 off";
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
  printResets();
  printSim();
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
  const char *const NAME[3] = {"Ret T1 A5", "Mix T2 A4", "Rad T3 A3"};
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
  Serial.println(F("# commands: ? status | h help | m1 m2 m3 mode (as the buttons) | rc clear reset counters"));
  Serial.println(F("#   csv / txt: 1 Hz log as CSV or as the readable status line (remembered)"));
  Serial.println(F("#   SIM: Ret Mix Rad Tout TR DP (or T1 T2 T3) + value: Rad=-15 or Rad -15. Rad= clears it,"));
  Serial.println(F("#        clr clears all, 30 min max. Tout is held at the value (feasibility uses it)."));
  Serial.println(F("#        The control logic and all faults use the forced value; display and banner show SIM."));
  Serial.println(F("#   t test mode on (Mode 1, idle) | x test mode off"));
  Serial.println(F("#   in test mode: f<0-100> fans % | p<0-100> P2 % (p0 = PEN off) | v1 / v0 V1 open / closed"));
  Serial.println(F("#                 V2: dg<deg> angle (dg45) | o<mm> opening (o5) | o<pct>% outdoor (o50%)"));
  Serial.println(F("#                 +<ms> / -<ms> raw open / close run (max 15000) | k park (homes) | s stop"));
  Serial.println(F("#   faults stay armed; test mode ends on x, a button, a fault, 12V loss or 30 min idle"));
}

static void testState() {
  Serial.print(F("# TEST fans ")); Serial.print(hwFanPct());
  Serial.print(F("% P2 ")); Serial.print(hwPen() ? F("on ") : F("off ")); Serial.print(hwPumpPct());
  Serial.print(F("% V1 ")); Serial.print(!V1_FITTED ? F("not fitted") : hwSen() ? F("open") : F("closed"));
  Serial.print(F(" V2 ")); Serial.print(valveDeg(), 1); Serial.print(F(" deg "));
  Serial.print(valveOpenMm(), 1); Serial.print(F(" mm ")); Serial.print(valveOutdoorPct(), 0);
  Serial.println(valveHomed() ? F("% outdoor") : F("% outdoor (not homed)"));
}

// ---- SIM overrides and reset counters ----
static uint32_t simAt;
static const uint8_t EE_RST = 4;   // EEPROM 4..7: resets seen (WDT, brown-out, external, power-on), saturating at 255
static const char *const RST_NAME[4] = {"WDT", "BOR", "EXT", "POR"};
static const uint8_t RST_BIT[4] = {WDRF, BORF, EXTRF, PORF};

static void countReset() {
  for (uint8_t i = 0; i < 4; i++) {
    if (!(resetFlags & _BV(RST_BIT[i]))) continue;
    uint8_t v = EEPROM.read(EE_RST + i);
    if (v == 0xFF) v = 0;              // blank EEPROM
    if (v < 254) EEPROM.update(EE_RST + i, v + 1);
  }
}

static void printResets() {
  Serial.print(F("# resets counted (rc clears):"));
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t v = EEPROM.read(EE_RST + i);
    Serial.print(' '); Serial.print(RST_NAME[i]); Serial.print(' '); Serial.print(v == 0xFF ? 0 : v);
  }
  Serial.print(F(" | this boot:"));
  if (!(resetFlags & 0x0F)) Serial.print(F(" none flagged (bootloader cleared MCUSR?)"));
  for (uint8_t i = 0; i < 4; i++) if (resetFlags & _BV(RST_BIT[i])) { Serial.print(' '); Serial.print(RST_NAME[i]); }
  Serial.print(F(" | up ")); Serial.print(millis() / 1000); Serial.println(F(" s"));
}

// 0..2 NTCs, 3 room T, 4 DP, 5 T_out. Printed by the first name; aliases below.
#define SIM_N 6
static const char *const SIM_NAME[SIM_N] = {"Ret", "Mix", "Rad", "TR", "DP", "Tout"};
struct SimAlias { const char *name; uint8_t idx; };
static const SimAlias SIM_ALIAS[] = {{"Tout", 5}, {"Ret", 0}, {"Mix", 1}, {"Rad", 2}, {"T1", 0}, {"T2", 1}, {"T3", 2},
                                     {"TR", 3}, {"DP", 4}};   // longest-prefix first where they overlap (Tout/TR/T1)
static float simGet(uint8_t i) { return i < 4 ? sensorsOverride(i) : i == 4 ? controlDpOverride() : controlToutOverride(); }
static void simSet(uint8_t i, float v) {
  if (i < 4) sensorsSetOverride(i, v);
  else if (i == 4) controlSetDpOverride(v);
  else controlSetToutOverride(v);
}

static void printSim() {
  if (!controlSimActive()) { Serial.println(F("# SIM: no overrides (live sensors)")); return; }
  Serial.print(F("# SIM overrides:"));
  for (uint8_t i = 0; i < SIM_N; i++) {
    float v = simGet(i);
    if (isnan(v)) continue;
    Serial.print(' '); Serial.print(SIM_NAME[i]); Serial.print('='); Serial.print(v, 2);
  }
  Serial.print(F("  (clear in ")); Serial.print((TEST_TIMEOUT_MS - (millis() - simAt)) / 60000UL);
  Serial.println(F(" min, or 'clr')"));
}

static void simClear(const __FlashStringHelper *why) {
  for (uint8_t i = 0; i < SIM_N; i++) simSet(i, NAN);
  Serial.print(F("# SIM overrides cleared (")); Serial.print(why); Serial.println(')');
}

// "Rad=-15", "Rad -15", "rad-15", "T2=12.5", "dp=8"; "Mix=" or "Mix=-" clears one. Names case-insensitive.
// Spaces are stripped before this, so a name followed directly by a number is a SIM command too. Must run before the
// one-letter commands: "tout-15" used to start test mode ('t').
static bool simCommand(char *s) {
  int8_t idx = -1;
  const char *v = nullptr;
  for (uint8_t k = 0; k < sizeof(SIM_ALIAS) / sizeof(SIM_ALIAS[0]) && idx < 0; k++) {
    uint8_t l = strlen(SIM_ALIAS[k].name);
    if (strncasecmp(s, SIM_ALIAS[k].name, l)) continue;
    const char *r = s + l;
    if (*r == '=') { idx = SIM_ALIAS[k].idx; v = r + 1; }
    else if (*r == '-' || *r == '+' || *r == '.' || (*r >= '0' && *r <= '9')) { idx = SIM_ALIAS[k].idx; v = r; }
  }
  if (idx < 0) {
    if (!strchr(s, '=')) return false;
    Serial.println(F("# SIM: names are Ret Mix Rad Tout TR DP (or T1 T2 T3), e.g. Rad=-15"));
    return true;
  }
  if (!*v || (*v == '-' && !v[1])) {
    simSet(idx, NAN);
  } else {
    float f = atof(v);
    if (f < -40 || f > 90) { Serial.println(F("# SIM: value out of -40..90")); return true; }
    if (idx == 3 && !sht[SHT_R].valid) { Serial.println(F("# SIM: TR needs a valid room SHT30")); return true; }
    simSet(idx, f);
    simAt = millis();
  }
  printSim();
  return true;
}

static void runCommand(char *s) {
  if (simCommand(s)) return;
  if (!strcmp(s, "clr")) { simClear(F("clr")); return; }
  if (!strcmp(s, "csv") || !strcmp(s, "txt")) {   // log format, kept in EEPROM 8
    csvMode = s[0] == 'c';
    EEPROM.update(EE_LOGFMT, csvMode ? 1 : 0);
    printHeader();
    return;
  }
  if (!strcmp(s, "rc")) {
    for (uint8_t i = 0; i < 4; i++) EEPROM.update(EE_RST + i, 0);
    printResets();
    return;
  }
  char c = s[0];
  long n = atol(s + 1);
  if (c == '?' && !s[1]) {
    printStatus();
    sensorsRoomDiag();
    reportEvents(true);
    return;
  }
  if (c == 'h' && !s[1]) { testHelp(); return; }
  if (c == 'm' && n >= 1 && n <= 3) {    // same as the buttons: also acknowledges faults whose cause is gone
    controlButton((Mode)n);
    Serial.print(F("# MODE ")); Serial.print(n); Serial.println(F(" (serial)"));
    return;
  }
  if (c == 't' && !s[1]) {
    if (controlTestEnter()) { Serial.println(F("# TEST mode ON")); testState(); }
    else Serial.println(F("# TEST refused: needs Mode 1 (BTN1), idle, 12V present and no fault"));
    return;
  }
  if (c == 'x' && !s[1]) {
    if (controlTesting()) { controlTestExit(); Serial.println(F("# TEST mode OFF, V2 parking")); }
    return;
  }
  if (!strchr("fpvodks+-", c)) { Serial.println(F("# ? unknown command, h for help")); return; }
  if (!controlTesting()) { Serial.println(F("# not in test mode: send t first (Mode 1)")); return; }
  bool ok = true;
  switch (c) {
    case 'f': controlTestFan(constrain(n, 0, 100)); break;
    case 'p': controlTestPump(constrain(n, 0, 100)); break;
    case 'v':
      if (!V1_FITTED) Serial.println(F("# V1 not fitted (V1_FITTED 0)"));
      controlTestV1(n == 1);
      break;
    case 'o': {                          // o<mm> or o<pct>% : go to that outdoor opening
      float v = atof(s + 1);
      bool pctIn = strchr(s, '%') != nullptr;
      if (!valveHomed()) { Serial.println(F("# V2 not homed: send k (park) first")); ok = true; break; }
      float target = pctIn ? valveDegForPct(v) : valveDegForMm(v);
      Serial.print(F("# V2 -> ")); Serial.print(target, 1); Serial.println(F(" deg"));
      ok = valveGoto(target);
      break;
    }
    case 'd':                            // dg<deg> : go to that angle (0 = bypass end, 90 = all ports open)
      if (s[1] != 'g') { Serial.println(F("# ? use dg<deg>, e.g. dg45")); return; }
      if (!valveHomed()) { Serial.println(F("# V2 not homed: send k (park) first")); ok = true; break; }
      ok = valveGoto(atof(s + 2));
      break;
    case '+': ok = valveJog(V_OPEN, constrain(n, 1, 15000)); break;    // raw timed run, ms
    case '-': ok = valveJog(V_CLOSE, constrain(n, 1, 15000)); break;
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
  csvMode = EEPROM.read(EE_LOGFMT) == 1;
  printHeader();
  Serial.print(F("# reset flags 0x"));
  Serial.println(resetFlags, HEX);
  if (resetFlags & _BV(WDRF)) Serial.println(F("# WARNING: watchdog reset"));
  if (resetFlags & _BV(BORF)) Serial.println(F("# WARNING: brown-out reset (5V dipped)"));
  countReset();
  printResets();

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
  if (controlSimActive() && now - simAt >= TEST_TIMEOUT_MS) simClear(F("30 min timeout"));

  displayUpdate(now);              // last: never in the safety path
}
