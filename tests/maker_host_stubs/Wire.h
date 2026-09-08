#pragma once
#include "Arduino.h"
struct TwoWire {
  bool begin(int,int) { return true; }
  void end() {}
  void setClock(unsigned) {}
  void setTimeOut(unsigned) {}
  void beginTransmission(unsigned) {}
  int endTransmission() { return 0; }
};
inline TwoWire Wire;
