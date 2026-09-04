#pragma once
#include <stdint.h>
#include <math.h>

// Pure motor-output state machine. Called by a dedicated task, not I2C polling.
namespace MotorSafety {
constexpr uint32_t TIMEOUT_MS = 300;
constexpr uint32_t REVERSE_PAUSE_MS = 150;
constexpr float SLEW_PER_MS = 255.0F / 300.0F;

struct Wheel {
  float applied = 0;
  int8_t lastSign = 0;
  uint32_t zeroSince = 0;
  bool atZero = true;
};

class Controller {
 public:
  Wheel wheels[4];
  float targets[4] = {};
  bool live = false;
  uint32_t lastCommand = 0;
  uint32_t lastTick = 0;

  void zeroWheel(unsigned i, uint32_t now) {
    if (!wheels[i].atZero) wheels[i].zeroSince = now;
    wheels[i].applied = 0;
    wheels[i].atZero = true;
  }
  void stop(uint32_t now) {
    live = false;
    for (unsigned i = 0; i < 4; ++i) {
      targets[i] = 0;
      zeroWheel(i, now);
    }
  }
  bool publish(const float values[4], uint32_t now) {
    for (unsigned i = 0; i < 4; ++i) {
      if (!isfinite(values[i]) || fabsf(values[i]) > 255.0F) {
        stop(now);
        return false;
      }
    }
    for (unsigned i = 0; i < 4; ++i) targets[i] = values[i];
    lastCommand = now;
    live = true;
    return true;
  }
  static float approach(float from, float to, float step) {
    if (from < to) return fminf(to, from + step);
    return fmaxf(to, from - step);
  }
  bool tick(uint32_t now) {
    uint32_t dt = now - lastTick;
    lastTick = now;
    if (dt > 10) dt = 10;  // Never jump to high duty after scheduling delays.
    if (live && now - lastCommand >= TIMEOUT_MS) {
      stop(now);
      return true;
    }
    if (!live) return false;
    for (unsigned i = 0; i < 4; ++i) {
      Wheel &w = wheels[i];
      if (targets[i] == 0) { zeroWheel(i, now); continue; }
      const int8_t sign = targets[i] > 0 ? 1 : -1;
      const float step = SLEW_PER_MS * dt;
      if (w.lastSign != 0 && sign != w.lastSign) {
        if (!w.atZero) {
          w.applied = approach(w.applied, 0, step);
          if (w.applied == 0) zeroWheel(i, now);
          continue;
        }
        if (now - w.zeroSince < REVERSE_PAUSE_MS) continue;
      }
      w.applied = approach(w.applied, targets[i], step);
      if (w.applied != 0) { w.atZero = false; w.lastSign = sign; }
    }
    return false;
  }
};
}  // namespace MotorSafety
