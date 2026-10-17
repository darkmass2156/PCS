#pragma once
#include <Arduino.h>

// V2 floating control: position estimate θ (0 = bypass/home, 90 = all ports open).
enum ValveDir : int8_t { V_CLOSE = -1, V_NONE = 0, V_OPEN = 1 };

void valveInit();
void valveUpdate(uint32_t now);
void valveSetPowerOk(bool ok);          // 12V and S3 present; false stops V2 and drops the home
bool valveStep(ValveDir dir, uint32_t ms); // ignored while busy; a close that reaches 0° becomes a park
void valvePark();                        // drive VC for t_close × overdrive, then θ = 0 and homed
void valveStop();
bool valveBusy();
bool valveHomed();
bool valveParked();                      // homed, θ = 0 and idle
float valveDeg();
ValveDir valveMoving();
