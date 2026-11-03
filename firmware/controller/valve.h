#pragma once
#include <Arduino.h>

// V2 floating control: position estimate θ (0 = bypass/home, 90 = all ports open).
enum ValveDir : int8_t { V_CLOSE = -1, V_NONE = 0, V_OPEN = 1 };

void valveInit();
void valveUpdate(uint32_t now);
void valveSetPowerOk(bool ok);          // 12V and S3 present; false stops V2 and drops the home
// Ignored while busy. An open from below the edge adds the dead-zone time; a close stops at the edge
// (outdoor shut, no park). Returns false when there is nothing to do.
bool valveStep(ValveDir dir, uint32_t ms);
void valvePark();                        // drive VC for V2_PARK_MS (autostop), then θ = 0 and homed
void valveStop();
bool valveJog(ValveDir dir, uint32_t ms);   // test mode: a plain timed run (no edge / dead-zone logic)
bool valveBusy();
bool valveHomed();
bool valveParked();                      // homed, θ = 0 and idle
bool valveShut();                        // homed, at or below the edge and idle: no outdoor flow
float valveDeg();
ValveDir valveMoving();
