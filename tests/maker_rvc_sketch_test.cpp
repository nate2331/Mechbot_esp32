#include <cassert>
#include <iostream>
#include "../Maker_IMU_UART_RVC_Check/Maker_IMU_UART_RVC_Check.ino"

unsigned resetLow = 0, resetHigh = 0;
void observeWrite(unsigned pin, unsigned value) {
  if (pin == P0_PIN) assert(pinModes[P0_PIN] == OUTPUT);
  if (pin == RST_PIN) {
    assert(pinModes[RST_PIN] == OUTPUT);
    if (value == LOW) ++resetLow;
    else { ++resetHigh; assert(pinDuty[P0_PIN] == HIGH); }
  }
}
void assertStopped() {
  for (uint8_t pin : MOTOR_PINS) {
    assert(pinDuty[pin] == LOW && pinModes[pin] == OUTPUT);
    assert(!pinAttached[pin]);
  }
}
int main() {
  digitalWriteHook = observeWrite;
  setup();
  assertStopped();
  assert(resetLow == 1 && resetHigh == 1);
  assert(sdaUart.rx == 21 && sclUart.rx == 22);
  for (auto &c : channels) {
    assert(c.port.tx == -1 && c.port.baud == 115200);
    assert(c.port.bufferSize == 2048);
  }
  assert(pinModes[32] == INPUT && pinModes[33] == INPUT && pinModes[26] == INPUT);
  Serial.output.clear();
  fakeNow += 1000;
  loop();
  assert(Serial.output.find("WAIT BYTES 0 FRAMES 0") != std::string::npos);
  const uint8_t sample[] = {0xAA,0xAA,0xDE,0x01,0x00,0x92,0xFF,0x25,0x08,
                           0x8D,0xFE,0xEC,0xFF,0xD1,0x03,0,0,0,0xE7};
  for (auto &c : channels) {
    for (uint8_t b : sample) c.port.input.push_back(b);
  }
  fakeNow += 1000;
  Serial.output.clear();
  loop();
  assert(Serial.output.find("LIVE BYTES 19 FRAMES 1 NEW 1") != std::string::npos);
  assert(Serial.output.find("YPR_DEG 0.01 -1.10 20.85 ACCEL_MG -371 -20 977") != std::string::npos);
  fakeNow += 1000;
  Serial.output.clear();
  loop();
  assert(Serial.output.find("STALE BYTES 19 FRAMES 1 NEW 0") != std::string::npos);
  for (unsigned i = 0; i < 60; ++i) { fakeNow += 1000; loop(); assertStopped(); }
  assert(resetLow == 1 && resetHigh == 1);
  assert(sdaUart.beginCalls == 1 && sclUart.beginCalls == 1);
  std::cout << "PASS: dual RX-only UART, one mode-ready reset, motor idle, live/wait/stale output\n";
}
