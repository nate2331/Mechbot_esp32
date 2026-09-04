#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#define CONFIG_IDF_TARGET_ESP32 1
#define IRAM_ATTR
#define DRAM_ATTR
#define OUTPUT 1
#define INPUT 2
#define INPUT_PULLUP 3
#define LOW 0
#define CHANGE 4
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE = int;
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
inline void portENTER_CRITICAL_ISR(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE*) {}
inline uint32_t fakeNow = 0;
inline unsigned pinModes[64] = {}, pinDuty[64] = {}, pinInput[64] = {};
inline bool pinAttached[64] = {};
inline void (*timeHook)() = nullptr;
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; if (timeHook) timeHook(); }
inline void pinMode(unsigned p, unsigned mode) { pinModes[p] = mode; }
inline void digitalWrite(unsigned p, unsigned value) { pinDuty[p] = value; }
inline unsigned digitalRead(unsigned p) { return pinInput[p]; }
inline unsigned digitalPinToInterrupt(unsigned p) { return p; }
inline void attachInterrupt(unsigned, void(*)(), unsigned) {}
inline bool ledcAttach(unsigned p, unsigned, unsigned) { pinAttached[p] = true; return true; }
inline bool ledcWrite(unsigned p, unsigned value) { pinDuty[p] = value; return true; }
template<class T> T constrain(T v, T a, T b) { return std::min(b, std::max(a,v)); }
struct FakeSerial {
  std::string input, output;
  void begin(unsigned) {}
  int available() { return static_cast<int>(input.size()); }
  int read() { const char c=input[0]; input.erase(0,1); return c; }
  void println(const char* s) { output += s; output += '\n'; }
  template<class... Args> void printf(const char* fmt, Args... args) {
    char buffer[2048]; std::snprintf(buffer,sizeof(buffer),fmt,args...); output += buffer;
  }
};
inline FakeSerial Serial;
