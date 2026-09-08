/*
  NULLLAB Maker ESP32 Pro: one-wheel-at-a-time, manually triggered bench test.
  a=M0/RR, b=M1/RL, c=M2/FL, d=M3/FR. Select a wheel before f/r.
  Board: ESP32 Dev Module (NOT ESP32-S3); Espressif Arduino core 3.x.
  Serial Monitor: 115200 baud, any line ending.
  f = forward pulse, r = reverse pulse, x = stop, ? = help.
  t = automatic RR, RL, FL, FR test, forward/reverse one wheel at a time.
  x cancels the sequence, including during pauses. No automatic motion on boot.
  e = encoder snapshot, z = zero counters while stopped.
  1 / 2 / 3 = select 177 / 200 / 230 out of 255 PWM while stopped.
  Starts stopped with 200/255 selected (approximately 78%).

  Lift ALL wheels clear of the floor. Set M2/M3 switches to Motor.
  Motor leads go to M0-M3, NOT the battery: battery goes to the DC input.
  Encoder mapping assumes matching ports: RR/E0, RL/E1, FL/E2, FR/E3.
  f/r target chassis forward/reverse based on the user's wheel tests.
  RR motor inverted; RR and FR encoder signs inverted. Verify wheels raised.
  Encoder A/B must be 3.3V safe with common GND. GPIO34-39 have no internal
  pull-ups: open-collector encoder outputs there need external 3.3V pull-ups.
  Encoder feedback is diagnostic only: no stall detection or current limiting.
  Low PWM does NOT guarantee low peak current. Cut power if it only buzzes.
  Pin reference: https://github.com/nulllaborg/maker-esp32-pro
*/
#include <Arduino.h>
#include <esp_arduino_version.h>

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "Select ESP32 Dev Module for the ESP32-WROOM board, not ESP32-S3."
#endif
#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "This sketch requires Espressif ESP32 Arduino core 3.x or newer."
#endif

struct MotorPins {
  const char *name;
  uint8_t a;
  uint8_t b;
  bool invertDirection;
};
constexpr MotorPins MOTORS[] = {
  {"M0/RR", 27, 13, true}, {"M1/RL", 4, 2, false},
  {"M2/FL", 17, 12, false}, {"M3/FR", 14, 15, false}
};
// Raw counts are kept in the ISR; normalize all reported counts in readEncoder.
constexpr int8_t ENCODER_POLARITY[] = {-1, +1, +1, -1};
int8_t selectedMotor = -1;  // Require explicit wheel selection after reset.
// Writable DRAM arrays keep pin lookups accessible from the ISR.
uint8_t encoderAPins[] = {18, 5, 35, 34};
uint8_t encoderBPins[] = {19, 23, 36, 39};
uint8_t encoderIndices[] = {0, 1, 2, 3};
constexpr uint32_t ENCODER_REPORT_MS = 100;
constexpr uint32_t PWM_HZ = 20000;
constexpr uint32_t PULSE_MS = 750;
constexpr uint32_t RAMP_MS = 200;
constexpr uint32_t COOLDOWN_MS = 1000;
constexpr uint8_t DUTIES[] = {177, 200, 230};
uint8_t selectedDuty = DUTIES[1];
uint32_t startedAt = 0;
uint32_t stoppedAt = 0;
bool running = false;
bool reverseDirection = false;
bool pwmReady = false;
constexpr uint32_t SEQUENCE_PAUSE_MS = 3000;
constexpr uint32_t ENCODER_QUIET_MS = 500;
constexpr uint32_t SEQUENCE_WAIT_TIMEOUT_MS = 10000;
bool sequenceActive = false;
uint8_t sequenceStep = 0;
uint32_t lastEncoderMotionAt = 0;

// x4 quadrature decoding: count valid changes on both A and B.
// The sign is relative to the A/B wiring, not an assumed wheel direction.
DRAM_ATTR const int8_t QUADRATURE_DELTA[16] = {
  0, -1, +1, 0, +1, 0, 0, -1,
  -1, 0, 0, +1, 0, +1, -1, 0
};
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int64_t encoderCount[4] = {};
volatile uint32_t aEdges[4] = {};
volatile uint32_t bEdges[4] = {};
volatile uint32_t invalidTransitions[4] = {};
volatile uint8_t previousAB[4] = {};

struct EncoderSnapshot {
  int64_t count;
  uint32_t a;
  uint32_t b;
  uint32_t invalid;
  uint8_t ab;
};
EncoderSnapshot pulseStart = {};
EncoderSnapshot lastReport[4] = {};
uint32_t lastReportAt = 0;

void IRAM_ATTR encoderISR(void *argument) {
  const uint8_t i = *static_cast<uint8_t *>(argument);
  portENTER_CRITICAL_ISR(&encoderMux);
  const uint8_t ab = (digitalRead(encoderAPins[i]) << 1) | digitalRead(encoderBPins[i]);
  const uint8_t changed = previousAB[i] ^ ab;
  if (changed & 2) ++aEdges[i];
  if (changed & 1) ++bEdges[i];
  if (changed == 3) ++invalidTransitions[i];  // Both changed: direction ambiguous.
  encoderCount[i] += QUADRATURE_DELTA[(previousAB[i] << 2) | ab];
  previousAB[i] = ab;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

EncoderSnapshot readEncoder(uint8_t i) {
  portENTER_CRITICAL(&encoderMux);
  const EncoderSnapshot snapshot = {
    encoderCount[i] * ENCODER_POLARITY[i], aEdges[i], bEdges[i], invalidTransitions[i], previousAB[i]
  };
  portEXIT_CRITICAL(&encoderMux);
  return snapshot;
}

void printEncoder() {
  for (uint8_t i = 0; i < 4; ++i) {
  const EncoderSnapshot s = readEncoder(i);
  Serial.printf("ENC %s/E%u count=%lld A=%u B=%u A_edges=%lu B_edges=%lu invalid=%lu\n",
                MOTORS[i].name, i,
                static_cast<long long>(s.count), (s.ab >> 1) & 1, s.ab & 1,
                static_cast<unsigned long>(s.a), static_cast<unsigned long>(s.b),
                static_cast<unsigned long>(s.invalid));
  }
}

void reportPulse() {
  Serial.printf("%s ", MOTORS[selectedMotor].name);
  const EncoderSnapshot s = readEncoder(selectedMotor);
  const int64_t delta = s.count - pulseStart.count;
  const uint32_t da = s.a - pulseStart.a;
  const uint32_t db = s.b - pulseStart.b;
  Serial.printf("PULSE at power-off: delta=%lld A_edges=%lu B_edges=%lu invalid=%lu\n",
                static_cast<long long>(delta), static_cast<unsigned long>(da),
                static_cast<unsigned long>(db),
                static_cast<unsigned long>(s.invalid - pulseStart.invalid));
  if (da == 0 && db == 0) {
    Serial.println("WARN: no encoder changes; check rotation, encoder power, GND and A/B wiring.");
  } else if (da == 0 || db == 0) {
    Serial.println("WARN: only one encoder channel changed; check both A/B connections.");
  } else if (delta == 0) {
    Serial.println("WARN: edges detected but zero net count; check noise, wiring or back-and-forth motion.");
  }
  if (sequenceActive && (da == 0 || db == 0 || delta == 0 || s.invalid != pulseStart.invalid)) {
    sequenceActive = false;
    Serial.println("SEQUENCE ABORTED: encoder check needs attention. No further pulses.");
  }
}

void stopMotor() {
  if (pwmReady) {
    for (const MotorPins &motor : MOTORS) {
      ledcWrite(motor.a, 0);
      ledcWrite(motor.b, 0);
    }
  }
  const bool wasRunning = running;
  running = false;
  stoppedAt = millis();
  if (wasRunning) reportPulse();
}

void startPulse() {
  pulseStart = readEncoder(selectedMotor);
  startedAt = millis();
  running = true;
  Serial.printf("%s %s pulse\n", MOTORS[selectedMotor].name,
                reverseDirection ? "reverse" : "forward");
}

void serviceSequence() {
  // Give incoming stop/cancel commands priority over starting another pulse.
  if (!sequenceActive || running || Serial.available()) return;
  const uint32_t now = millis();
  if (now - stoppedAt < SEQUENCE_PAUSE_MS) return;
  if (now - lastEncoderMotionAt < ENCODER_QUIET_MS) {
    if (now - stoppedAt >= SEQUENCE_WAIT_TIMEOUT_MS) {
      sequenceActive = false;
      stopMotor();
      Serial.println("SEQUENCE ABORTED: encoders did not settle. Check motion/noise.");
    }
    return;
  }
  if (sequenceStep >= 8) {
    sequenceActive = false;
    Serial.println("SEQUENCE COMPLETE: 8 pulses finished. Check physical directions and encoder summaries.");
    return;
  }
  selectedMotor = sequenceStep / 2;
  reverseDirection = (sequenceStep % 2) != 0;
  ++sequenceStep;
  Serial.printf("TEST %u/8 | PWM %u/255\n", sequenceStep, selectedDuty);
  startPulse();
}

void printHelp() {
  Serial.println("ONE WHEEL ONLY | a=M0/RR b=M1/RL c=M2/FL d=M3/FR");
  Serial.println("Select wheel, then f=forward 750ms | r=reverse 750ms | x=STOP ALL");
  Serial.println("t=AUTO RR/RL/FL/FR forward+reverse; x=CANCEL, even during pauses.");
  Serial.println("AUTO: 3s minimum pauses + 0.5s encoder quiet; aborts on encoder-check faults.");
  Serial.println("POLARITY V2: RR motor inverted; RR/FR encoder signs inverted.");
  Serial.println("f=chassis forward (+counts); r=reverse (-counts). Verify wheels raised.");
  Serial.println("1=177 (69%), 2=200 (78%), 3=230 (90%) PWM /255; ?=help");
  Serial.println("Select PWM while stopped; selection alone does not move the motor.");
  Serial.println("e=all encoder snapshots; z=zero all encoders while stopped.");
  Serial.println("RR/E0 A18 B19 | RL/E1 A5 B23 | FL/E2 A35 B36 | FR/E3 A34 B39");
  Serial.println("ENC reports x4 counts, counts/s, channel edges and invalid transitions.");
  Serial.println("Wait 1 second between pulses. Wheel off ground. No stall protection.");
  Serial.printf("Selected PWM: %u/255\n", selectedDuty);
  Serial.printf("Selected wheel: %s\n", selectedMotor < 0 ? "NONE" : MOTORS[selectedMotor].name);
}

void setup() {
  // Establish stopped outputs before enabling PWM, including unused channels.
  const uint8_t motorPins[] = {27, 13, 4, 2, 17, 12, 14, 15};
  for (uint8_t pin : motorPins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  Serial.begin(115200);
  for (uint8_t i = 0; i < 4; ++i) {
    pinMode(encoderAPins[i], encoderAPins[i] >= 34 ? INPUT : INPUT_PULLUP);
    pinMode(encoderBPins[i], encoderBPins[i] >= 34 ? INPUT : INPUT_PULLUP);
    previousAB[i] = (digitalRead(encoderAPins[i]) << 1) | digitalRead(encoderBPins[i]);
    attachInterruptArg(digitalPinToInterrupt(encoderAPins[i]), encoderISR, &encoderIndices[i], CHANGE);
    attachInterruptArg(digitalPinToInterrupt(encoderBPins[i]), encoderISR, &encoderIndices[i], CHANGE);
    lastReport[i] = readEncoder(i);
  }
  lastReportAt = millis();
  bool attached[8] = {};
  pwmReady = true;
  for (uint8_t i = 0; i < 8; ++i) {
    attached[i] = ledcAttach(motorPins[i], PWM_HZ, 8);
    if (attached[i]) ledcWrite(motorPins[i], 0);
    pwmReady = pwmReady && attached[i];
  }
  if (!pwmReady) {
    for (uint8_t i = 0; i < 8; ++i) {
      if (attached[i]) ledcDetach(motorPins[i]);
      pinMode(motorPins[i], OUTPUT);
      digitalWrite(motorPins[i], LOW);
    }
    Serial.println("FATAL: PWM setup failed. Motion disabled; reset to retry.");
    return;
  }
  stopMotor();
  printHelp();
}

void loop() {
  if (!pwmReady) { delay(1); return; }
  // Nonblocking pulse: the deadline is checked even while serial data arrives.
  if (running) {
    const uint32_t elapsed = millis() - startedAt;
    if (elapsed >= PULSE_MS) {
      stopMotor();
      Serial.println("STOP: pulse complete");
    } else {
      const uint8_t duty = elapsed < RAMP_MS
          ? static_cast<uint8_t>(selectedDuty * elapsed / RAMP_MS)
          : selectedDuty;
      // The opposite input remains zero throughout this pulse.
      const MotorPins &motor = MOTORS[selectedMotor];
      const bool electricalReverse = reverseDirection != motor.invertDirection;
      ledcWrite(electricalReverse ? motor.b : motor.a, duty);
    }
  }
  const uint32_t now = millis();
  if (now - lastReportAt >= ENCODER_REPORT_MS) {
    for (uint8_t i = 0; i < 4; ++i) {
    const EncoderSnapshot s = readEncoder(i);
    if (s.a != lastReport[i].a || s.b != lastReport[i].b) lastEncoderMotionAt = now;
    if ((running && selectedMotor == i) || s.a != lastReport[i].a || s.b != lastReport[i].b) {
      const float cps = (s.count - lastReport[i].count) * 1000.0F / (now - lastReportAt);
      Serial.printf("ENC %s/E%u count=%lld cps=%.1f A=%u B=%u A_edges=%lu B_edges=%lu invalid=%lu\n",
                    MOTORS[i].name, i,
                    static_cast<long long>(s.count), cps, (s.ab >> 1) & 1, s.ab & 1,
                    static_cast<unsigned long>(s.a), static_cast<unsigned long>(s.b),
                    static_cast<unsigned long>(s.invalid));
    }
    lastReport[i] = s;
    }
    lastReportAt = now;
  }
  serviceSequence();
  if (!Serial.available()) return;
  const char command = static_cast<char>(Serial.read());
  if (command == '\r' || command == '\n' || command == ' ') return;
  if (command == 'x' || command == 'X') {
    sequenceActive = false;
    stopMotor();
    Serial.println("STOP: requested; automatic sequence canceled");
  } else if (command == '?') {
    printHelp();
  } else if ((command >= 'a' && command <= 'd') || (command >= 'A' && command <= 'D')) {
    if (sequenceActive) { Serial.println("Send x to cancel the sequence before selecting a wheel."); return; }
    if (running) { Serial.println("STOP first with x before selecting another wheel."); return; }
    stopMotor();
    selectedMotor = command >= 'a' ? command - 'a' : command - 'A';
    Serial.printf("Selected %s; no motion. Wait 1 second, then f/r.\n", MOTORS[selectedMotor].name);
  } else if (command == 'e' || command == 'E') {
    printEncoder();
  } else if (command == 'z' || command == 'Z') {
    if (sequenceActive) { Serial.println("Send x to cancel the sequence before zeroing."); return; }
    if (running) { Serial.println("STOP first with x before zeroing."); return; }
    portENTER_CRITICAL(&encoderMux);
    for (uint8_t i = 0; i < 4; ++i) {
      encoderCount[i] = 0;
      aEdges[i] = bEdges[i] = invalidTransitions[i] = 0;
      previousAB[i] = (digitalRead(encoderAPins[i]) << 1) | digitalRead(encoderBPins[i]);
    }
    portEXIT_CRITICAL(&encoderMux);
    for (uint8_t i = 0; i < 4; ++i) lastReport[i] = readEncoder(i);
    lastReportAt = millis();
    Serial.println("Encoder counters zeroed; wait for coasting to stop before measuring.");
  } else if (command >= '1' && command <= '3') {
    if (sequenceActive) { Serial.println("Send x to cancel the sequence before changing PWM."); return; }
    if (running) { Serial.println("STOP first with x before changing PWM."); return; }
    selectedDuty = DUTIES[command - '1'];
    Serial.printf("Selected PWM: %u/255 (no motion)\n", selectedDuty);
  } else if (command == 't' || command == 'T') {
    if (running || sequenceActive) { Serial.println("BUSY: send x to cancel before restarting."); return; }
    stopMotor();
    sequenceStep = 0;
    lastEncoderMotionAt = millis();
    sequenceActive = true;
    Serial.println("SEQUENCE ARMED: wheels raised! First pulse after >=3s. Send x to cancel.");
  } else if (command == 'f' || command == 'F' || command == 'r' || command == 'R') {
    if (sequenceActive) { Serial.println("Send x to cancel the sequence before manual motion."); return; }
    if (selectedMotor < 0) { Serial.println("Select a wheel first: a=RR b=RL c=FL d=FR."); return; }
    if (running || millis() - stoppedAt < COOLDOWN_MS) {
      Serial.println("BUSY: wait until stopped for 1 second; command ignored.");
      return;
    }
    reverseDirection = command == 'r' || command == 'R';
    startPulse();
  } else {
    sequenceActive = false;
    stopMotor();
    Serial.println("STOP: unknown command. Send ? for help.");
  }
}
