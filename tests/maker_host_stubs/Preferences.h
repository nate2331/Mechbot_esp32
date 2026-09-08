#pragma once
#include "Arduino.h"
struct Preferences {
  bool begin(const char*,bool) { return true; }
  void end() {}
  uint8_t getUChar(const char*,uint8_t v) { return v; }
  float getFloat(const char*,float v) { return v; }
  int8_t getChar(const char*,int8_t v) { return v; }
  bool getBool(const char*,bool v) { return v; }
  unsigned putUChar(const char*,uint8_t) { return 1; }
  unsigned putChar(const char*,int8_t) { return 1; }
  unsigned putBool(const char*,bool) { return 1; }
  unsigned putFloat(const char*,float) { return sizeof(float); }
};
