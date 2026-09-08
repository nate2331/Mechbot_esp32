// CUSTOM passive UART-RVC diagnostic; not a STRDC SDK example.
// Existing wiring: 3V3->VDC, GND, SDA->8, SCL->9, RST->4, P0->5, INT->6.
// P1 remains pulled LOW, BT remains pulled HIGH by the breakout.
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include "RvcParser.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module."
#endif

constexpr uint8_t RX_PIN = 8, SCL_PIN = 9, RST_PIN = 4, P0_PIN = 5, INT_PIN = 6;
constexpr uint32_t TEST_MS = 35000;
RvcParser parser;
RvcParser::Sample previousSample;
bool active = false, haveResult = false;
uint32_t trial = 0, startMs = 0, lastStatusMs = 0, endMs = 0;
uint32_t bytesRead = 0, firstByteMs = 0, lastByteMs = 0;
uint32_t firstFrameMs = 0, lastFrameMs = 0, priorFrames = 0;
uint32_t freshIndexed = 0, repeatedIndex = 0, orientationChanges = 0, accelChanges = 0;
uint8_t prefix[128] = {};
unsigned prefixCount = 0;

void printStats(const char *tag, uint32_t now) {
  const uint32_t frames = parser.validFrames();
  Serial.printf("%s #%lu t=%lu %s bytes=%lu frames=%lu new=%lu fresh_index=%lu repeated_index=%lu bad=%lu discontinuities=%lu age=%ld byte_age=%ld\n",
    tag, (unsigned long)trial, (unsigned long)(now - startMs),
    !frames ? "WAIT" : now - lastFrameMs > 500 ? "STALE" : "LIVE",
    (unsigned long)bytesRead, (unsigned long)frames, (unsigned long)(frames - priorFrames),
    (unsigned long)freshIndexed, (unsigned long)repeatedIndex,
    (unsigned long)parser.badChecksums(), (unsigned long)parser.indexDiscontinuities(),
    frames ? (long)(now - lastFrameMs) : -1L, bytesRead ? (long)(now - lastByteMs) : -1L);
  if (frames) {
    const auto &s = parser.sample();
    Serial.printf(" VALUE index=%u ypr_deg=%.2f,%.2f,%.2f accel_mg=%d,%d,%d changes_ypr=%lu changes_accel=%lu first=%lu last=%lu\n",
      s.index, s.yaw * 0.01, s.pitch * 0.01, s.roll * 0.01, s.ax, s.ay, s.az,
      (unsigned long)orientationChanges, (unsigned long)accelChanges,
      (unsigned long)(firstFrameMs - startMs), (unsigned long)(lastFrameMs - startMs));
  }
}

void restoreI2C() {
  // Stop the BNO transmitter before taking GPIO8 back for I2C.
  digitalWrite(RST_PIN, LOW);
  Serial1.end();
  pinMode(RX_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  digitalWrite(P0_PIN, LOW);
  delay(20);
  digitalWrite(RST_PIN, HIGH);
  delay(400);
  const bool busOk = Wire.begin(RX_PIN, SCL_PIN, 100000);
  Wire.setTimeOut(50);
  Serial.printf("RESTORE I2C commanded P0=%d RST=%d bus=%d\n",
    digitalRead(P0_PIN), digitalRead(RST_PIN), busOk);
  if (busOk) {
    for (uint8_t address = 0x4A; address <= 0x4B; ++address) {
      Wire.beginTransmission(address);
      const uint8_t rc = Wire.endTransmission();
      Serial.printf(" RESTORE ACK 0x%02X rc=%u\n", address, rc);
    }
    Wire.end();
  }
  pinMode(RX_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
}

void finish(const char *reason) {
  if (!active) return;
  active = false;
  haveResult = true;
  endMs = millis();
  Serial.printf("RESULT reason=%s\n", reason);
  printStats("FINAL", endMs);
  Serial.printf("PREFIX bytes=%u first_byte=%ld last_byte=%ld HEX", prefixCount,
    bytesRead ? (long)(firstByteMs - startMs) : -1L,
    bytesRead ? (long)(lastByteMs - startMs) : -1L);
  for (unsigned i = 0; i < prefixCount; ++i) Serial.printf(" %02X", prefix[i]);
  Serial.println();
  Serial.print("PREFIX ASCII ");
  for (unsigned i = 0; i < prefixCount; ++i)
    Serial.write(prefix[i] >= 32 && prefix[i] <= 126 ? prefix[i] : '.');
  Serial.println();
  restoreI2C();
  Serial.printf("END RVC #%lu; return-to-I2C reset sent; r=repeat, s=snapshot\n", (unsigned long)trial);
}

void startTrial() {
  if (active) return;
  ++trial;
  haveResult = false;
  parser = RvcParser();
  previousSample = RvcParser::Sample();
  bytesRead = firstByteMs = lastByteMs = firstFrameMs = lastFrameMs = priorFrames = 0;
  freshIndexed = repeatedIndex = orientationChanges = accelChanges = 0;
  prefixCount = 0;
  digitalWrite(RST_PIN, LOW);
  // With the sensor held reset, select UART-RVC and attach only host RX.
  digitalWrite(P0_PIN, HIGH);
  pinMode(SCL_PIN, INPUT);  // The SCL/RX wire is never driven in RVC mode.
  Serial1.setRxBufferSize(2048);
  Serial1.begin(115200, SERIAL_8N1, RX_PIN, -1);
  if (!Serial1) {
    Serial.println("FAIL UART_RX_START");
    restoreI2C();
    Serial.println("END RVC UART_FAILED");
    return;
  }
  // Explicit RX prevents the current ESP32 core from assigning default TX pins.
  delay(20);
  Serial.printf("BEGIN RVC #%lu RX8=IMU_SDA_TX baud=115200 8N1 frame=19 P0=1 RSTlow20ms\n", (unsigned long)trial);
  startMs = lastStatusMs = millis();
  active = true;
  digitalWrite(RST_PIN, HIGH);
}

void setup() {
  Serial.begin(115200);
  const uint32_t before = millis();
  while (!Serial && millis() - before < 3000) delay(10);
  pinMode(RST_PIN, OUTPUT); digitalWrite(RST_PIN, LOW);
  pinMode(P0_PIN, OUTPUT); digitalWrite(P0_PIN, LOW);
  pinMode(RX_PIN, INPUT); pinMode(SCL_PIN, INPUT); pinMode(INT_PIN, INPUT);
  delay(500);
  Serial.println("IMU_RVC_TEST V1 CUSTOM | RX8 RST4 P0=5 | no UART TX pin");
  Serial.println("Automatic one 35-second trial; x=stop and restore I2C; r=repeat when idle; s=snapshot");
  startTrial();
}

void loop() {
  if (Serial.available()) {
    const char c = Serial.read();
    if (c == 'x') finish("USER_STOP");
    else if (c == 'r' && !active) startTrial();
    else if (c == 's') {
      if (active) printStats("SNAP", millis());
      else if (haveResult) printStats("SAVED", endMs);
    }
  }
  // A bounded drain keeps status/finish responsive even if UART receives noise.
  for (unsigned i = 0; active && i < 512 && Serial1.available(); ++i) {
    const int input = Serial1.read();
    if (input < 0) break;
    const uint8_t byte = static_cast<uint8_t>(input);
    const uint32_t now = millis();
    if (!bytesRead) firstByteMs = now;
    ++bytesRead;
    lastByteMs = now;
    if (prefixCount < sizeof(prefix)) prefix[prefixCount++] = byte;
    const uint32_t previousCount = parser.validFrames();
    if (!parser.feed(byte)) continue;
    const auto &s = parser.sample();
    if (!previousCount) { firstFrameMs = now; ++freshIndexed; }
    else {
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
  if (active) {
    const uint32_t now = millis();
    if (now - startMs >= TEST_MS) finish("35_SECONDS");
    else if (now - lastStatusMs >= 1000) {
      printStats("RUN", now);
      priorFrames = parser.validFrames();
      lastStatusMs = now;
    }
  }
  delay(1);
}
