// Verify the automatic diagnostic's behavior using fake IO, without hardware.
#include <cassert>
#include <iostream>
#include "../Maker_IMU_Error_Check/Maker_IMU_Error_Check.ino"

void assertStopped() {
  for (uint8_t pin : MOTOR_PINS) {
    assert(pinModes[pin] == OUTPUT && pinDuty[pin] == LOW);
    assert(!pinAttached[pin]);
  }
}

int main(int argc, char**) {
  const bool failure = argc > 1;
  pinInput[INT_PIN] = LOW;
  imu.beginSucceeds = !failure;
  setup();
  assertStopped();
  assert(imu.beginCalls == 1 && imu.hardwareResetCalls == 1);
  assert(initialized == !failure);
  assert(SPI.sck == 22 && SPI.miso == 21 && SPI.mosi == 32 && SPI.ss == 33);
  assert(imu.resetPin == 25 && imu.spiIntPin == 26 && WAKE_PIN == 16);
  assert(Serial.output.find("CHECK RESULT BEGIN") != std::string::npos);
  assert(Serial.output.find("CHECK RESULT END") != std::string::npos);
  assert(Serial.output.find("highest priority; does not check every severity") != std::string::npos);
  assert(Serial.output.find("ERROR LOG INCOMPLETE") != std::string::npos);
  assert(Serial.txBufferSizeAtBegin == 2048);
  for (unsigned report : {SH2_GAME_ROTATION_VECTOR, SH2_GYROSCOPE_CALIBRATED,
                          SH2_LINEAR_ACCELERATION})
    assert(imu.enableReportCalls[report] == (failure ? 0U : 1U));
  const auto writes = imu.diagnostics().txPackets;
  assert(writes == (failure ? 0U : 3U)); // Stub enableReport does not send bytes.
  assert(millis() < 5000); // Automatic probe waits have a finite overall budget.

  // A minute with no reports, including a reset notification and arbitrary
  // incoming serial text, must never trigger a write, reset, or motor output.
  imu.resetPending = true;
  Serial.input = "V 1 1 1\nIMU RETRY\n";
  Serial.output.clear();
  for (unsigned i = 0; i < 60; ++i) {
    fakeNow += 1000;
    loop();
    assertStopped();
  }
  assert(imu.beginCalls == 1 && imu.hardwareResetCalls == 1);
  assert(imu.diagnostics().txPackets == writes);
  assert(Serial.output.find("CHECK RESULT BEGIN") != std::string::npos);
  assert(Serial.output.find("CHECK RESULT END") != std::string::npos);
  std::cout << "PASS: automatic error-check sketch ("
            << (failure ? "startup failure" : "startup success")
            << ") stays stopped and does not retry or requery\n";
}
