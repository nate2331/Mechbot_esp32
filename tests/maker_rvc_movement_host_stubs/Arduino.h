#pragma once
// Isolated host IO for the motor/IMU exercise; never opens hardware.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#define CONFIG_IDF_TARGET_ESP32 1
#define OUTPUT 1
#define INPUT 2
#define LOW 0
#define HIGH 1
#define portMUX_INITIALIZER_UNLOCKED 0
using portMUX_TYPE = int;
inline void portENTER_CRITICAL(portMUX_TYPE*) {}
inline void portEXIT_CRITICAL(portMUX_TYPE*) {}
inline uint32_t fakeNow = 0;
inline unsigned pinModes[64] = {}, pinDuty[64] = {};
inline bool pinAttached[64] = {};
inline bool fakeLedcAttachOk = true;
inline unsigned pinChannel[64] = {}, pinFrequency[64] = {}, pinResolution[64] = {};
constexpr int LEDC_USE_APB_CLK = 1;
inline int fakeLedcClock = 0;
inline bool fakeLedcClockOk = true;
inline unsigned fakeLedcWritesToFail = 0;
inline bool ledcSetClockSource(int source) { fakeLedcClock = source; return fakeLedcClockOk; }
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; }
inline void pinMode(unsigned pin, unsigned mode) { pinModes[pin] = mode; }
inline void digitalWrite(unsigned pin, unsigned value) { pinDuty[pin] = value; }
inline bool ledcAttach(unsigned pin, unsigned, unsigned) {
  pinAttached[pin] = fakeLedcAttachOk;
  return fakeLedcAttachOk;
}
inline bool ledcAttachChannel(unsigned pin, unsigned freq, unsigned bits, unsigned channel) {
  pinAttached[pin] = fakeLedcAttachOk;
  pinFrequency[pin] = freq; pinResolution[pin] = bits; pinChannel[pin] = channel;
  return fakeLedcAttachOk;
}
inline bool ledcWrite(unsigned pin, unsigned value) {
  if (fakeLedcWritesToFail) { --fakeLedcWritesToFail; return false; }
  pinDuty[pin] = value; return true;
}
inline void ledcDetach(unsigned pin) { pinAttached[pin] = false; }
template<class A, class B> auto min(A a, B b) -> std::common_type_t<A, B> { return a < b ? a : b; }
template<class A, class B> auto max(A a, B b) -> std::common_type_t<A, B> { return a > b ? a : b; }
struct FakeSerial {
  std::string input, output;
  bool begun = false;
  size_t txBufferSize = 0;
  size_t setTxBufferSize(size_t size) { return txBufferSize = size; }
  void begin(unsigned) { begun = true; }
  int available() { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    unsigned char c = input[0]; input.erase(0, 1); return c;
  }
  void print(const char* s) { output += s; }
  void println(const char* s = "") { output += s; output += '\n'; }
  void printf(const char* text) { output += text; }
  template<class... Args> void printf(const char* fmt, Args... args) {
    char buffer[4096]; std::snprintf(buffer, sizeof(buffer), fmt, args...); output += buffer;
  }
};
inline FakeSerial Serial;
