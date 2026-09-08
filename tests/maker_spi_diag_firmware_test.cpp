// Exercise the diagnostic sketch against fake IO; never opens hardware.
#include <cassert>
#include <iostream>
#include <vector>
#include "../Maker_Mechbot_SPI_Diag/Maker_Mechbot_SPI_Diag.ino"

void command(const char *text) {
  std::vector<char> line(text, text + std::strlen(text) + 1);
  processCommand(line.data());
}

void assertStopped() {
  assert(!commandActive && !motionRequested && !outputController.live);
  for (const auto &m : motors)
    assert(pinDuty[m.dir1Pin] == 0 && pinDuty[m.dir2Pin] == 0);
}

void pollForMinute() {
  for (unsigned i = 0; i < 600; ++i) {
    fakeNow += 100;
    pollImu();
  }
}

int main() {
  pinInput[IMU_INT_PIN] = LOW;
  setup();
  assertStopped();
  assert(imuAvailable && bno08x.hardwareResetCalls == 1);
  assert(SPI.sck == 22 && SPI.miso == 21 && SPI.mosi == 32 && SPI.ss == 33);
  assert(bno08x.resetPin == 25 && bno08x.spiIntPin == 26 && IMU_WAKE_PIN == 16);
  assert(Serial.output.find("MAKER_SPI_V3C_SINGLE_TX") != std::string::npos);

  // Missing streams and the startup reset notification must not cause retries.
  const unsigned reports = bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR];
  bno08x.resetPending = true;
  pollForMinute();
  assert(bno08x.hardwareResetCalls == 1 && bno08x.beginCalls == 1);
  assert(bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR] == reports);
  assert(imuReportRetryCount == 0);

  // A previously valid but stalled stream must also remain untouched.
  imuQuaternionValid = imuGyroValid = imuAccelerationValid = true;
  lastImuQuaternionMs = lastImuGyroMs = lastImuAccelerationMs = millis();
  pollForMinute();
  assert(bno08x.hardwareResetCalls == 1);
  assert(bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR] == reports);

  for (const char *input : {"V 1 0 0", "V 0 -1 1", " V 1 1 1", "V nan 0 0"}) {
    command(input);
    for (unsigned i = 0; i < 50; ++i) {
      fakeNow += 5;
      outputController.tick(millis());
      for (uint8_t j = 0; j < MOTOR_COUNT; ++j)
        writeMotorDuty(j, outputController.wheels[j].applied);
    }
    assertStopped();
  }

  // Read-only diagnostics must not reset or reconfigure the sensor.
  Serial.output.clear();
  command("IMU TRACE");
  command("IMU DIAG");
  assert(Serial.output.find("IMU TRACE BEGIN") != std::string::npos);
  assert(Serial.output.find("IMU TRACE END") != std::string::npos);
  assert(bno08x.beginCalls == 1);

  // Manual retries work, including recovery from a failed initialization.
  bno08x.beginSucceeds = false;
  command("IMU RETRY");
  assert(!imuAvailable && bno08x.hardwareResetCalls == 2);
  pollForMinute();
  assert(bno08x.beginCalls == 2 && bno08x.hardwareResetCalls == 2);
  bno08x.beginSucceeds = true;
  command("IMU RETRY");
  assert(imuAvailable && bno08x.hardwareResetCalls == 3);
  assertStopped();
  // Probe may read configuration/product IDs but must never reset or enable reports.
  const unsigned probeReports = bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR];
  const uint32_t probeStart = millis();
  Serial.output.clear();
  command("IMU PROBE");
  assert(millis() - probeStart >= 1000 && millis() - probeStart <= 1102);
  assert(bno08x.hardwareResetCalls == 3);
  assert(bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR] == probeReports);
  assert(Serial.output.find("IMU PROBE SENT FEATURE 1 PRODUCT 1") != std::string::npos);
  assertStopped();
  std::cout << "PASS: diagnostic disables motion and automatic recovery; manual retry and read-only trace remain available\n";
}
