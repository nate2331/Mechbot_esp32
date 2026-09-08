// CUSTOM passive UART-RVC diagnostic, not an unchanged vendor example.
// IMU SDA/MISO/TX -> verified SN74LVC245AN 5V-tolerant input -> 3V3 output -> GPIO21.
// The buffer is powered at 3V3; IMU VDC is externally powered at 5V.
// P0 is strapped to IMU VDC; P1/BT retain onboard LOW/HIGH defaults.
// Disconnect all old host RST/P0/SCL/INT wires. Only GPIO21 receives IMU data; the eight motor inputs are held LOW.
// Leave the IMU unpowered until the receiver prints ARMED after command 'r'.
#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp32-hal-uart.h>
#include <atomic>
#include "RvcParser.h"

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "Select ESP32 Dev Module for the NULLLAB Maker ESP32 Pro."
#endif

constexpr int8_t RX_PIN = 21;
constexpr uint8_t MOTOR_PINS[] = {27, 13, 4, 2, 17, 12, 14, 15};
constexpr uint32_t WAIT_MS = 120000, CAPTURE_MS = 35000;
constexpr size_t RX_BUFFER_BYTES = 8192;
enum class Phase : uint8_t { Idle, WaitHigh, Armed, Capture };
Phase phase = Phase::Idle;
bool receiverReady = false, haveResult = false;
bool highSeen = false;
uint32_t highSinceMs = 0;
uint32_t trial = 0, armMs = 0, captureMs = 0, endMs = 0, lastStatusMs = 0;
uint32_t bytesRead = 0, firstByteMs = 0, lastByteMs = 0;
uint32_t firstFrameMs = 0, lastFrameMs = 0, priorFrames = 0;
uint32_t freshIndexed = 0, repeatedIndex = 0, orientationChanges = 0, accelChanges = 0;
RvcParser parser;
RvcParser::Sample previousSample;
uint8_t prefix[128] = {};
size_t prefixCount = 0;

// Arduino delivers error callbacks on its UART event task. Atomic counters avoid
// races with the loop task. These count reported events, not exact lost bytes.
std::atomic<bool> countErrors{false};
std::atomic<uint32_t> uartErrors[6] = {};
uint32_t savedErrors[6] = {};

void receiveError(hardwareSerial_error_t error) {
  const unsigned index = static_cast<unsigned>(error);
  if (countErrors.load(std::memory_order_relaxed) && index > 0 && index < 6)
    uartErrors[index].fetch_add(1, std::memory_order_relaxed);
}

void printErrors(bool frozen) {
  uint32_t e[6] = {};
  for (unsigned i = 1; i < 6; ++i)
    e[i] = frozen ? savedErrors[i] : uartErrors[i].load(std::memory_order_relaxed);
  Serial.printf(" UART_EVENTS fifo_overflow=%lu buffer_full=%lu frame=%lu parity=%lu break=%lu\n",
    (unsigned long)e[UART_FIFO_OVF_ERROR], (unsigned long)e[UART_BUFFER_FULL_ERROR],
    (unsigned long)e[UART_FRAME_ERROR], (unsigned long)e[UART_PARITY_ERROR],
    (unsigned long)e[UART_BREAK_ERROR]);
}

void printStats(const char* tag, uint32_t now, bool frozen) {
  const uint32_t frames = parser.validFrames();
  Serial.printf("%s #%lu arm_ms=%lu capture_ms=%lu %s bytes=%lu frames=%lu new=%lu fresh_index=%lu repeated_index=%lu bad_checksum=%lu discontinuities=%lu frame_age_ms=%ld byte_age_ms=%ld\n",
    tag, (unsigned long)trial, (unsigned long)(now - armMs),
    (unsigned long)(bytesRead ? now - captureMs : 0),
    !frames ? "NO_FRAMES" : now - lastFrameMs > 500 ? "STALE" : "LIVE",
    (unsigned long)bytesRead, (unsigned long)frames, (unsigned long)(frames - priorFrames),
    (unsigned long)freshIndexed, (unsigned long)repeatedIndex,
    (unsigned long)parser.badChecksums(), (unsigned long)parser.indexDiscontinuities(),
    frames ? (long)(now - lastFrameMs) : -1L, bytesRead ? (long)(now - lastByteMs) : -1L);
  if (frames) {
    const auto& s = parser.sample();
    Serial.printf(" VALUE index=%u ypr_deg=%.2f,%.2f,%.2f accel_mg=%d,%d,%d changes_ypr=%lu changes_accel=%lu first_frame_ms=%lu last_frame_ms=%lu\n",
      s.index, s.yaw * 0.01, s.pitch * 0.01, s.roll * 0.01, s.ax, s.ay, s.az,
      (unsigned long)orientationChanges, (unsigned long)accelChanges,
      (unsigned long)(firstFrameMs - captureMs), (unsigned long)(lastFrameMs - captureMs));
  }
  printErrors(frozen);
}

void help() {
  Serial.println("MAKER_IMU_RVC_5V_TEST V1 CUSTOM | UART1 RX21 TX=none | 115200 8N1 | frame=19 | RX buffer=8192");
  Serial.println("Only the buffer's 3V3 output connects to GPIO21. IMU TX must not connect directly to Maker.");
  Serial.println("Keep IMU 5V OFF. Send r; after ARMED, apply IMU 5V. GPIO21 must idle HIGH for 2ms before UART starts.");
  Serial.println("Then the first byte starts a 35-second capture. Boot ASCII counts as first data.");
  Serial.println("No first byte within 120 seconds of r -> finite timeout, including time waiting for idle HIGH.");
  Serial.println("r=arm when idle, x=stop, s=current/saved snapshot, ?=help. No automatic trial or retry.");
  Serial.println("No sensor power/reset/mode control. After END, turn IMU 5V OFF before another r.");
}

void finish(const char* reason) {
  if (phase == Phase::Idle) return;
  countErrors.store(false, std::memory_order_relaxed);
  // Stop the UART event task before freezing its counters. Return only RX21 to input.
  if (receiverReady) Serial1.end();
  receiverReady = false;
  pinMode(RX_PIN, INPUT);
  for (unsigned i = 1; i < 6; ++i)
    savedErrors[i] = uartErrors[i].load(std::memory_order_relaxed);
  phase = Phase::Idle;
  haveResult = true;
  endMs = millis();
  Serial.printf("RESULT #%lu reason=%s\n", (unsigned long)trial, reason);
  printStats("FINAL", endMs, true);
  Serial.printf("PREFIX bytes=%u first_byte_after_arm_ms=%ld last_byte_after_first_ms=%ld HEX",
    (unsigned)prefixCount, bytesRead ? (long)(firstByteMs - armMs) : -1L,
    bytesRead ? (long)(lastByteMs - firstByteMs) : -1L);
  for (size_t i = 0; i < prefixCount; ++i) Serial.printf(" %02X", prefix[i]);
  Serial.println();
  Serial.print("PREFIX ASCII ");
  for (size_t i = 0; i < prefixCount; ++i)
    Serial.write(prefix[i] >= 32 && prefix[i] <= 126 ? prefix[i] : '.');
  Serial.println();
  Serial.println("Counts are newly received packets. Repeated values can be normal; index gaps are not exact lost-packet counts.");
  Serial.println("UART overflow events mean capture may be incomplete. No frames alone does not prove physical damage.");
  Serial.printf("END #%lu RX remains input; IMU POWER IS STILL EXTERNAL. Turn IMU 5V OFF before r.\n", (unsigned long)trial);
}

void armTrial() {
  if (phase != Phase::Idle) {
    Serial.println("BUSY: current trial preserved; x stops it.");
    return;
  }
  countErrors.store(false, std::memory_order_relaxed);
  if (receiverReady) Serial1.end();
  receiverReady = false;
  pinMode(RX_PIN, INPUT);
  for (unsigned i = 0; i < 6; ++i) {
    uartErrors[i].store(0, std::memory_order_relaxed);
    savedErrors[i] = 0;
  }
  parser = RvcParser();
  previousSample = RvcParser::Sample();
  bytesRead = firstByteMs = lastByteMs = firstFrameMs = lastFrameMs = priorFrames = 0;
  freshIndexed = repeatedIndex = orientationChanges = accelChanges = 0;
  prefixCount = 0;
  captureMs = endMs = 0;
  highSeen = false;
  highSinceMs = 0;
  haveResult = false;
  ++trial;
  armMs = lastStatusMs = millis();
  phase = Phase::WaitHigh;
  Serial.printf("ARMED #%lu RX21 input. APPLY IMU 5V NOW; waiting for idle HIGH >=2ms, then first byte (120s total).\n", (unsigned long)trial);
}

void beginAfterIdleHigh(uint32_t now) {
  // With IMU power off its TX pullup can pull the buffer input LOW. Attaching
  // UART too early could turn that into break/framing bytes and start the timer.
  // Start only after a sampled 2ms idle HIGH; the prior board's boot banner was
  // observed about164ms after reset. Sampling is not proof of power or no noise.
  if (digitalRead(RX_PIN) != HIGH) {
    highSeen = false;
    return;
  }
  if (!highSeen) {
    highSeen = true;
    highSinceMs = now;
    return;
  }
  if (now - highSinceMs < 2) return;
  const size_t allocated = Serial1.setRxBufferSize(RX_BUFFER_BYTES);
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, -1);
  // Core3.3.11 assigns defaults only when BOTH requested pins are negative.
  const int actualRx = uart_get_RxPin(1), actualTx = uart_get_TxPin(1);
  receiverReady = Serial1 && allocated >= RX_BUFFER_BYTES && actualRx == RX_PIN && actualTx == -1;
  Serial.printf("UART_CHECK ok=%u rx=%d tx=%d buffer=%u after_arm_ms=%lu\n",
    receiverReady, actualRx, actualTx, (unsigned)allocated, (unsigned long)(millis() - armMs));
  if (!receiverReady) {
    Serial1.end();
    finish("UART_INIT_FAILED");
    return;
  }
  Serial1.eventQueueReset();
  Serial1.onReceiveError(receiveError);
  countErrors.store(true, std::memory_order_relaxed);
  phase = Phase::Armed;
  Serial.println("UART_LISTENING: idle HIGH passed; waiting for first byte.");
}

void consume(uint8_t byte, uint32_t now) {
  if (phase == Phase::Armed) {
    phase = Phase::Capture;
    captureMs = firstByteMs = lastStatusMs = now;
    Serial.printf("FIRST_BYTE #%lu after_arm_ms=%lu; capture window=35000ms\n",
      (unsigned long)trial, (unsigned long)(now - armMs));
  }
  ++bytesRead;
  lastByteMs = now;
  if (prefixCount < sizeof(prefix)) prefix[prefixCount++] = byte;
  const uint32_t previousCount = parser.validFrames();
  if (!parser.feed(byte)) return;
  const auto& s = parser.sample();
  if (!previousCount) {
    firstFrameMs = now;
    ++freshIndexed;
  } else {
    if (s.index == previousSample.index) ++repeatedIndex;
    else ++freshIndexed;
    if (s.yaw != previousSample.yaw || s.pitch != previousSample.pitch || s.roll != previousSample.roll)
      ++orientationChanges;
    if (s.ax != previousSample.ax || s.ay != previousSample.ay || s.az != previousSample.az)
      ++accelChanges;
  }
  previousSample = s;
  lastFrameMs = now;
}

void setup() {
  // Hold all eight onboard motor-driver inputs LOW. No PWM is attached.
  // Bench power is USB only; leave external DC/battery disconnected.
  for (const uint8_t pin : MOTOR_PINS) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  const uint32_t before = millis();
  while (!Serial && millis() - before < 3000) delay(10);
  pinMode(RX_PIN, INPUT);
  help();
  Serial.println("IDLE: leave IMU unpowered; send r when ready.");
}

void loop() {
  // Timeout checks precede reading, so bytes after a completed window are not counted.
  uint32_t now = millis();
  if ((phase == Phase::WaitHigh || phase == Phase::Armed) && now - armMs >= WAIT_MS)
    finish(phase == Phase::WaitHigh ? "NO_IDLE_HIGH_120_SECONDS" : "NO_DATA_120_SECONDS");
  else if (phase == Phase::Capture && now - captureMs >= CAPTURE_MS) finish("35_SECONDS_FROM_FIRST_BYTE");

  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'r') armTrial();
    else if (command == 'x') {
      if (phase != Phase::Idle) finish("USER_STOP");
      else Serial.println("IDLE: no active trial.");
    } else if (command == 's') {
      if (phase != Phase::Idle) printStats("SNAP", millis(), false);
      else if (haveResult) printStats("SAVED", endMs, true);
      else Serial.println("IDLE: no saved trial; r arms receiver.");
    } else if (command == '?') help();
  }

  if (phase == Phase::WaitHigh) beginAfterIdleHigh(millis());

  // Bounded drain serves a full-rate noisy UART without starving commands/deadlines.
  for (unsigned i = 0; receiverReady && i < 1024 && Serial1.available(); ++i) {
    now = millis();
    if (phase == Phase::Armed && now - armMs >= WAIT_MS) {
      finish("NO_DATA_120_SECONDS");
      break;
    }
    if (phase == Phase::Capture && now - captureMs >= CAPTURE_MS) {
      finish("35_SECONDS_FROM_FIRST_BYTE");
      break;
    }
    const int input = Serial1.read();
    if (input < 0) break;
    if (phase != Phase::Idle) consume(static_cast<uint8_t>(input), now);
    // Idle bytes are intentionally discarded; frozen results remain unchanged.
  }

  now = millis();
  if ((phase == Phase::WaitHigh || phase == Phase::Armed) && now - lastStatusMs >= 5000) {
    // The top-of-loop deadline may have just been crossed during console output.
    const uint32_t waited = now - armMs;
    const uint32_t remaining = waited >= WAIT_MS ? 0 : WAIT_MS - waited;
    Serial.printf("WAIT #%lu phase=%s elapsed_s=%lu remaining_s=%lu; apply IMU 5V\n",
      (unsigned long)trial, phase == Phase::WaitHigh ? "IDLE_HIGH" : "FIRST_BYTE",
      (unsigned long)(waited / 1000), (unsigned long)(remaining / 1000));
    lastStatusMs = now;
  } else if (phase == Phase::Capture && now - lastStatusMs >= 1000) {
    printStats("RUN", now, false);
    priorFrames = parser.validFrames();
    lastStatusMs = now;
  }
  delay(1);
}
