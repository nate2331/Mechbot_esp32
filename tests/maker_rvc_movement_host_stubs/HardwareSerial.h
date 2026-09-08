#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include "esp32-hal-uart.h"
#define SERIAL_8N1 0x800001c
inline bool fakeUartBeginOk = true;
inline bool fakeUartWrongRx = false;
class HardwareSerial {
 public:
  explicit HardwareSerial(int port) : number(port) {}
  int number, rx = -1, tx = -1;
  unsigned long baud = 0;
  unsigned beginCalls = 0, endCalls = 0;
  std::size_t bufferSize = 0;
  bool ready = false;
  std::deque<uint8_t> input;
  void (*errorCallback)(hardwareSerial_error_t) = nullptr;
  std::size_t setRxBufferSize(std::size_t n) { bufferSize = n; return n; }
  void begin(unsigned long speed, uint32_t, int8_t receive, int8_t transmit) {
    baud = speed; rx = receive; tx = transmit; ++beginCalls; ready = fakeUartBeginOk;
    fakeUartRx[number] = fakeUartWrongRx ? 22 : rx; fakeUartTx[number] = tx;
  }
  explicit operator bool() const { return ready; }
  void end() { ready = false; ++endCalls; }
  int available() { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    const auto result = input.front(); input.pop_front(); return result;
  }
  void onReceiveError(void (*callback)(hardwareSerial_error_t)) { errorCallback = callback; }
};
inline HardwareSerial Serial1(1);
