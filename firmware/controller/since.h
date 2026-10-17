#pragma once
#include <Arduino.h>

// "Condition has held since ..." timer. Wrap-safe; no magic zero value.
struct Since {
  bool active;
  uint32_t at;
  void hold(uint32_t now) {
    if (!active) {
      active = true;
      at = now;
    }
  }
  void clear() { active = false; }
  bool elapsed(uint32_t now, uint32_t ms) const { return active && now - at >= ms; }
};
