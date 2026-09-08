#pragma once
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#define CONFIG_IDF_TARGET_ESP32 1
#define IRAM_ATTR
#define DRAM_ATTR
#define OUTPUT 1
#define INPUT 2
#define INPUT_PULLUP 3
#define LOW 0
#define HIGH 1
#define CHANGE 4
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
using std::max;
enum ledc_clk_cfg_t { LEDC_AUTO_CLK, LEDC_USE_APB_CLK, LEDC_USE_REF_TICK };

namespace fake {
struct Pin { int mode=0, level=1; bool attached=false, enabled=false; uint32_t duty=0,hz=0; uint8_t bits=0; };
struct Write { uint32_t at; uint8_t pin; uint32_t duty; bool accepted; };
inline std::array<Pin,64> pins;
inline std::array<bool,64> inputs{};
inline std::array<void(*)(),64> handlers{};
inline uint32_t now=0;
inline int attachCalls=0, writeCalls=0, failAttachAt=0, failWriteAt=0;
inline bool failPositiveWrite=false, failDetach=false, failClock=false;
inline int timerCalls=0, channelCalls=0, stopCalls=0, setDutyCalls=0, updateCalls=0;
inline int failTimerAt=0, failChannelAt=0, failStopAt=0, failSetDutyAt=0, failUpdateAt=0;
inline int unsafeTimerChanges=0;
inline uint32_t timerHz=0;
inline uint8_t timerBits=0;
inline std::array<int,8> channelPins{{-1,-1,-1,-1,-1,-1,-1,-1}};
inline std::array<uint32_t,8> pendingDuty{};
inline int64_t readbackOverride=-1;
inline bool serialBeganAfterOutputsLow=false;
inline std::vector<Write> writes;
inline ledc_clk_cfg_t clock=LEDC_AUTO_CLK;
inline bool outputsLow() {
  for (int p : {17,12,14,15,4,2,27,13})
    if (pins[p].level!=LOW || (pins[p].attached && pins[p].enabled && pins[p].duty)) return false;
  return true;
}
inline uint32_t bank(bool high) {
  uint32_t v=0;
  for(int p=high?32:0;p<(high?64:32);++p) if(inputs[p]) v|=1u<<(p%32);
  return v;
}
}
inline uint32_t micros() { return fake::now; }
inline void delay(uint32_t ms) { fake::now += ms*1000u; }
inline void pinMode(uint8_t pin,int mode) {
  fake::pins[pin].mode=mode;
  // Model normal GPIO ownership after LEDC detach/peripheral release.
  if(mode==OUTPUT) { fake::pins[pin].attached=false; fake::pins[pin].enabled=false; fake::pins[pin].duty=0; }
}
inline void digitalWrite(uint8_t pin,int value) { fake::pins[pin].level=value; }
inline int digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int pin,void(*handler)(),int) { fake::handlers[pin]=handler; }
inline bool ledcSetClockSource(ledc_clk_cfg_t clock) {
  if(fake::failClock) return false;
  fake::clock=clock; return true;
}
inline bool ledcAttachChannel(uint8_t pin,uint32_t hz,uint8_t bits,uint8_t) {
  if(++fake::attachCalls==fake::failAttachAt) return false;
  auto &p=fake::pins[pin]; p.attached=true; p.enabled=true; p.duty=0; p.hz=hz; p.bits=bits; return true;
}
inline bool ledcWrite(uint8_t pin,uint32_t duty) {
  const bool ok=++fake::writeCalls!=fake::failWriteAt &&
    !(fake::failPositiveWrite && duty>0) && fake::pins[pin].attached;
  fake::writes.push_back({fake::now,pin,duty,ok});
  if(ok) { fake::pins[pin].duty=duty; fake::pins[pin].enabled=true; }
  return ok;
}
inline bool ledcDetach(uint8_t pin) {
  if(fake::failDetach) return false;
  fake::pins[pin].attached=false; fake::pins[pin].duty=0; return true;
}
inline uint32_t ledcReadFreq(uint8_t pin) {
  if(fake::readbackOverride>=0) return uint32_t(fake::readbackOverride);
  return fake::pins[pin].attached && fake::pins[pin].duty ? fake::pins[pin].hz : 0;
}
struct FakeSerial {
  std::deque<char> incoming;
  std::string output;
  void begin(int) { fake::serialBeganAfterOutputsLow=fake::outputsLow(); }
  int available() { return int(incoming.size()); }
  int read() { char c=incoming.front(); incoming.pop_front(); return c; }
  void feed(const std::string &s) { for(char c:s) incoming.push_back(c); }
  void print(const char *s) { output+=s; }
  void println(const char *s) { output+=s; output+='\n'; }
  void printf(const char *format,...) {
    va_list args; va_start(args,format);
    va_list copy; va_copy(copy,args);
    int size=vsnprintf(nullptr,0,format,copy); va_end(copy);
    std::vector<char> data(size+1); vsnprintf(data.data(),data.size(),format,args); va_end(args);
    output.append(data.data(),size);
  }
};
inline FakeSerial Serial;
struct FakeEsp { const char *getSdkVersion() { return "HOST-SIMULATION"; } };
inline FakeEsp ESP;
