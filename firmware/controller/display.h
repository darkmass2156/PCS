#pragma once
#include <Arduino.h>

void displayInit();                // blocking, called once in setup() before the watchdog starts
void displayUpdate(uint32_t now);  // non-blocking: at most one field or one init step per call
