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
#if NTC_TRIMPOT_SIM
  Serial.print(F("# SIM BUILD: NTC inputs are trimpots"));
  if (SIM_FORCE_12V) Serial.print(F(", 12V forced present"));
  if (SIM_FAKE_P2_TACH) Serial.print(F(", P2 tach faked"));
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
  Serial.print(' '); Serial.println(sht[SHT_R].errCount);
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
  while (Serial.available()) {
    if (Serial.read() == '?') printStatus();
  }

  displayUpdate(now);              // last: never in the safety path
}
