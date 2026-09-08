// Compile the actual standalone sketch with fake IO; never touches robot hardware.
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include "../Maker_IMU_RVC_Movement_Test/Maker_IMU_RVC_Movement_Test.ino"

namespace {
uint8_t nextFrameIndex = 0;
uint32_t lastInjectedMs = UINT32_MAX - 20;
uint32_t lastOutputTickMs = 0;

void queueFrame(bool badChecksum = false, unsigned skip = 0) {
  nextFrameIndex = static_cast<uint8_t>(nextFrameIndex + skip);
  std::array<uint8_t, 19> frame = {
      0xAA, 0xAA, nextFrameIndex++, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 0xD0, 3, 0, 0, 0, 0};
  for (unsigned i = 2; i < 18; ++i) frame[18] += frame[i];
  if (badChecksum) frame[18] ^= 1;
  for (uint8_t byte : frame) Serial1.input.push_back(byte);
}

void stopped() {
  for (unsigned pin : MOTOR_PINS) assert(pinDuty[pin] == 0);
}

void expectedWheelDuties(unsigned move, unsigned duty) {
  // Independent expected physical pins, not values computed from sketch tables.
  const unsigned pins[4][2] = {{17, 12}, {14, 15}, {4, 2}, {27, 13}};
  const int electrical[4][4] = {
      {1, 1, 1, -1}, {-1, -1, -1, 1}, {-1, 1, 1, 1}, {1, -1, -1, -1}};
  for (unsigned wheel = 0; wheel < 4; ++wheel) {
    const unsigned active = electrical[move][wheel] > 0 ? 0 : 1;
    assert(pinDuty[pins[wheel][active]] == duty * 2); // 8-bit logical -> 9-bit hardware duty.
    assert(pinDuty[pins[wheel][1 - active]] == 0);
  }
}

void command(const char* value) {
  Serial.input += value;
  unsigned iterations = 0;
  while (Serial.available()) {
    readCommands();
    assert(++iterations < 100);
  }
}

void runFor(uint32_t duration, bool feed = true) {
  const uint32_t begin = fakeNow;
  while (fakeNow - begin < duration) {
    if (feed && fakeNow - lastInjectedMs >= 10) {
      queueFrame();
      lastInjectedMs = fakeNow;
    }
    loop(); // Arduino delay(1) advances the fake clock.
    if (fakeNow - lastOutputTickMs >= 5) {
      serviceMotorOutputs(fakeNow);
      lastOutputTickMs = fakeNow;
    }
  }
  // Exercise the exact boundary, including the 1 s output deadline.
  checkStale(fakeNow);
  updateMotion(fakeNow);
  serviceMotorOutputs(fakeNow);
  lastOutputTickMs = fakeNow;
}

void runTo(uint32_t deadline, bool feed = true) { runFor(deadline - fakeNow, feed); }

void startForward() {
  assert(ready);
  command("GO\n");
  assert(motionState == MotionState::COUNTDOWN);
  const uint32_t start = phaseSinceMs;
  stopped();
  runTo(start + COUNTDOWN_MS - 1);
  assert(motionState == MotionState::COUNTDOWN);
  stopped();
  runFor(1);
  assert(motionState == MotionState::MOVING && moveIndex == 0);
}

void checkBootAndMap() {
  setup();
  assert(motionState == MotionState::IDLE && !motionActive());
  assert(motorReady && motorMutex && fakeTaskCreateCalls == 1 && fakeMotorTask);
  assert(uartReady && Serial1.rx == 21 && Serial1.tx == -1);
  assert(Serial1.baud == 115200 && Serial1.bufferSize == 8192 && Serial1.errorCallback);
  stopped();
  assert(fakeLedcClock == LEDC_USE_APB_CLK);
  // Classic ESP32 LEDC's divider ceiling must accommodate the selected setup.
  assert(80000000.0 / (248 * 512) <= 1024);
  bool seenChannel[8] = {};
  for (unsigned pin : MOTOR_PINS) {
    assert(pinModes[pin] == OUTPUT && pinAttached[pin]);
    assert(pinFrequency[pin] == 248 && pinResolution[pin] == 9);
    assert(pinChannel[pin] < 8 && !seenChannel[pinChannel[pin]]);
    seenChannel[pinChannel[pin]] = true;
  }
  command("GO\n");
  assert(!motionActive());
  stopped();
  runFor(60);
  assert(ready && acquisitions == 1);
  assert(motionState == MotionState::IDLE);
  for (unsigned move = 0; move < 4; ++move) {
    for (unsigned wheel = 0; wheel < 4; ++wheel)
      writeMotorDuty(wheel, MOVE_SIGNS[move][wheel] * TEST_PWM);
    expectedWheelDuties(move, TEST_PWM);
  }
  zeroMotorOutputs();
}

void checkSequenceAndTelemetry() {
  startForward();
  for (unsigned phase = 0; phase < 8; ++phase) {
    assert(motionState == MotionState::MOVING && moveIndex == phase % 4);
    const uint32_t begin = phaseSinceMs;
    runTo(begin + 250);
    expectedWheelDuties(phase % 4, TEST_PWM);
    if (phase == 0) {
      const uint32_t frames = parser.validFrames();
      const uint32_t bad = parser.badChecksums();
      const uint32_t gaps = parser.indexDiscontinuities();
      queueFrame(true);
      runFor(1, false);
      assert(parser.badChecksums() == bad + 1);
      queueFrame(false, 2);
      runFor(1, false);
      assert(parser.validFrames() == frames + 1);
      assert(parser.indexDiscontinuities() > gaps);
      Serial1.errorCallback(UART_FRAME_ERROR);
      Serial1.errorCallback(UART_FIFO_OVF_ERROR);
      Serial.output.clear();
      stats(fakeNow);
      assert(errors[UART_FRAME_ERROR] == 1 && errors[UART_FIFO_OVF_ERROR] == 1);
      assert(Serial.output.find("COUNTS bad_checksum=1") != std::string::npos);
      assert(Serial.output.find("UART_EVENTS") != std::string::npos);
      assert(motionState == MotionState::MOVING);
      expectedWheelDuties(phase % 4, TEST_PWM);
    }
    runTo(begin + 999);
    assert(motionState == MotionState::MOVING);
    runFor(1);
    assert(motionState == MotionState::COAST);
    stopped();
    const uint32_t coastStart = phaseSinceMs;
    runTo(coastStart + 299);
    assert(motionState == MotionState::COAST);
    stopped();
    runFor(1);
    assert(motionState == MotionState::MOVING);
  }
  assert(cycles == 3 && moveIndex == 0);
  runFor(250);
  expectedWheelDuties(0, TEST_PWM);
  command("X"); // No newline or future output-task tick needed to stop.
  stopped();
  command("\n"); // Finish the X line before a later GO line.
  assert(motionState == MotionState::IDLE);
  runFor(1500);
  assert(motionState == MotionState::IDLE);
  stopped();
}

void checkStaleStopAndRecovery() {
  startForward();
  const uint32_t lastGood = lastFrameMs;
  runTo(lastGood + 500, false);
  assert(ready && motionState == MotionState::MOVING);
  runFor(1, false);
  assert(!ready && stale && motionState == MotionState::FAULT);
  stopped();
  command("GO\n");
  assert(!motionActive());
  runFor(100);
  assert(ready);
  assert(motionState == MotionState::FAULT);
  stopped();
  runFor(1500);
  assert(motionState == MotionState::FAULT);
  stopped();
}

void checkIndependentWatchdog() {
  startForward();
  runFor(250);
  expectedWheelDuties(0, TEST_PWM);
  const uint32_t lastMainLoop = fakeNow;
  // No loop(), command processing, UART work, or publishing for 300 ms.
  for (unsigned elapsed = 5; elapsed < 300; elapsed += 5) {
    fakeNow = lastMainLoop + elapsed;
    serviceMotorOutputs(fakeNow);
    assert(!motorWatchdogTripped);
  }
  fakeNow = lastMainLoop + 300;
  serviceMotorOutputs(fakeNow);
  assert(motorWatchdogTripped && !outputController.live);
  stopped();
  runFor(100);
  assert(motionState == MotionState::FAULT);
  assert(ready);
  stopped();
  runFor(1000);
  assert(motionState == MotionState::FAULT);
  stopped();
}

void checkIndependentMoveDeadline() {
  startForward();
  const uint32_t begin = phaseSinceMs;
  runTo(begin + 900);
  expectedWheelDuties(0, TEST_PWM);
  // Last heartbeat is recent, so this stop must be the 1 s phase deadline.
  fakeNow = begin + 999;
  serviceMotorOutputs(fakeNow);
  expectedWheelDuties(0, TEST_PWM);
  fakeNow = begin + 1000;
  serviceMotorOutputs(fakeNow);
  stopped();
  assert(!motorWatchdogTripped && !moveWindowActive && !outputController.live);
  command("X\n");
}

void checkClockWrap() {
  command("X\n");
  // Move the idle simulation close to wrap, maintaining a valid stream.
  fakeNow = UINT32_MAX - 1500;
  outputController.lastTick = fakeNow;
  lastOutputTickMs = fakeNow;
  lastFrameMs = lastInjectedMs = fakeNow;
  startForward(); // Countdown crosses uint32_t rollover.
  assert(fakeNow < 2000);
  for (unsigned phase = 0; phase < 4; ++phase) {
    assert(moveIndex == phase);
    runFor(250);
    expectedWheelDuties(phase, TEST_PWM);
    runTo(phaseSinceMs + 1000);
    assert(motionState == MotionState::COAST);
    stopped();
    runFor(300);
    assert(motionState == MotionState::MOVING);
  }
  command("X");
  stopped();
}

void checkWriteFailureLatch() {
  command("\n");
  startForward();
  runFor(250);
  expectedWheelDuties(0, TEST_PWM);
  fakeLedcWritesToFail = 1;
  serviceMotorOutputs(fakeNow);
  assert(motorWriteFailed && !outputController.live);
  stopped();
  runFor(1);
  assert(motionState == MotionState::FAULT);
  command("GO\n");
  assert(motionState == MotionState::FAULT);
  stopped();
}
} // namespace

int main(int argc, char** argv) {
  if (argc > 1) {
    const std::string failure = argv[1];
    const bool uartFailure = failure == "uart-failure" || failure == "uart-rx-failure";
    if (failure == "attach-failure") fakeLedcAttachOk = false;
    else if (failure == "task-failure") fakeTaskCreateResult = 0;
    else if (failure == "mutex-failure") fakeMutexOk = false;
    else if (failure == "clock-failure") fakeLedcClockOk = false;
    else if (failure == "uart-failure") fakeUartBeginOk = false;
    else if (failure == "uart-rx-failure") fakeUartWrongRx = true;
    else assert(false);
    setup();
    if (uartFailure) assert(motorReady && !uartReady && Serial1.endCalls == 1);
    else assert(!motorReady);
    assert(!motionActive());
    stopped();
    runFor(100);
    assert(ready != uartFailure);
    command("GO\n");
    assert(!motionActive());
    stopped();
    std::cout << "PASS: " << failure << " leaves all motors stopped and rejects GO\n";
    return 0;
  }
  checkBootAndMap();
  checkSequenceAndTelemetry();
  checkStaleStopAndRecovery();
  checkIndependentWatchdog();
  checkIndependentMoveDeadline();
  checkClockWrap();
  checkWriteFailureLatch();
  std::cout << "PASS: safe idle/arming, physical wheel map, 1 s moves/300 ms coasts, "
               "instant X, stale latch/recovery, independent watchdog/deadline, telemetry, clock wrap, PWM fault\n";
}
