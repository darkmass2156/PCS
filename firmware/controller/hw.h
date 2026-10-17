#pragma once
#include <Arduino.h>

void hwAllOutputsLow();          // first thing in setup()
void hwInit();                   // PWM timer, tach interrupts, inputs
void hwSetFanPct(uint8_t pct);
void hwSetPumpPct(uint8_t pct);
void hwSetPen(bool on);
void hwSetSen(bool on);
bool hwPen();
bool hwSen();
uint8_t hwFanPct();
uint8_t hwPumpPct();

// Call often; recomputes rpm once per second.
void hwUpdateTach(uint32_t now);
extern uint16_t rpmFan1, rpmFan2, rpmPump;

// Fuse sense with hysteresis and debounce. Index 0 = S1 (P2), 1 = S2 (fans), 2 = S3 (V1+V2).
void hwUpdateFuses(uint32_t now);
extern bool fusePresent[3];
extern uint16_t fuseAdc[3];
bool hw12vPresent();             // false when all three are absent (PC off), stable for V12_STABLE_MS
