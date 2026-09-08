// Passive BNO085 UART-RVC test. No SH-2 library or report-enable commands.
#include <Arduino.h>
#include <HardwareSerial.h>
#include "RvcParser.h"

constexpr uint8_t MOTOR_PINS[] = {17, 12, 14, 15, 4, 2, 27, 13};
constexpr uint8_t P0_PIN = 16, RST_PIN = 25;
HardwareSerial sdaUart(1), sclUart(2);

struct RvcChannel {
  HardwareSerial &port;
  const uint8_t pin;
  RvcParser parser;
  bool ready = false;
  uint32_t bytes = 0, lastValidMs = 0, lastPrintedFrames = 0;
  uint8_t prefix[24] = {};
  unsigned prefixCount = 0;
  RvcChannel(HardwareSerial &serial, uint8_t rx) : port(serial), pin(rx) {}
};
RvcChannel channels[] = {{sdaUart, 21}, {sclUart, 22}};
uint32_t lastSummaryMs = 0;

void printSummary() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - lastSummaryMs;
  for (auto &c : channels) {
    const uint32_t frames = c.parser.validFrames();
    const uint32_t added = frames - c.lastPrintedFrames;
    const uint32_t age = now - c.lastValidMs;
    const char *state = !c.ready ? "UART_FAILED" : !frames ? "WAIT" : age > 500 ? "STALE" : "LIVE";
    Serial.printf("RVC RX%u MS %lu %s BYTES %lu FRAMES %lu NEW %lu HZ %.1f BAD %lu INDEX_GAPS %lu AGE_MS %ld\n",
                  c.pin, static_cast<unsigned long>(now), state,
                  static_cast<unsigned long>(c.bytes), static_cast<unsigned long>(frames),
                  static_cast<unsigned long>(added), elapsed ? added * 1000.0 / elapsed : 0.0,
                  static_cast<unsigned long>(c.parser.badChecksums()),
                  static_cast<unsigned long>(c.parser.indexDiscontinuities()),
                  frames ? static_cast<long>(age) : -1L);
    if (frames) {
      const auto &s = c.parser.sample();
      Serial.printf("RVC RX%u INDEX %u YPR_DEG %.2f %.2f %.2f ACCEL_MG %d %d %d\n",
                    c.pin, s.index, s.yaw * 0.01, s.pitch * 0.01, s.roll * 0.01,
                    s.ax, s.ay, s.az);
    } else if (c.prefixCount) {
      Serial.printf("RVC RX%u FIRST_BYTES", c.pin);
      for (unsigned i = 0; i < c.prefixCount; ++i) Serial.printf(" %02X", c.prefix[i]);
      Serial.println("");
    }
    c.lastPrintedFrames = frames;
  }
  lastSummaryMs = now;
}

void setup() {
  for (uint8_t pin : MOTOR_PINS) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  // Neither serial port transmits. The old SPI control lines are not driven.
  for (uint8_t pin : {uint8_t(32), uint8_t(33), uint8_t(26)}) pinMode(pin, INPUT);
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  Serial.println("FIRMWARE MAKER_IMU_UART_RVC_CHECK_V1; MOTORS DISABLED");
  Serial.println("MODE: IMU P1 must be GND; P0 GPIO16 held HIGH; RST GPIO25");
  Serial.println("RX-only on SDA GPIO21 and SCL GPIO22, 115200 baud; no transmit pins");

  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  pinMode(P0_PIN, OUTPUT);
  digitalWrite(P0_PIN, HIGH);
  for (auto &c : channels) {
    c.port.setRxBufferSize(2048);
    // Core 3.3.11 leaves TX unattached when RX is explicitly supplied.
    c.port.begin(115200, SERIAL_8N1, c.pin, -1);
    c.ready = static_cast<bool>(c.port);
  }
  delay(10);
  digitalWrite(RST_PIN, HIGH); // Exactly one reset, after mode and RX are ready.
  lastSummaryMs = millis();
}

void loop() {
  for (auto &c : channels) {
    for (unsigned i = 0; c.ready && i < 512 && c.port.available(); ++i) {
      const int value = c.port.read();
      if (value < 0) break;
      const auto byte = static_cast<uint8_t>(value);
      ++c.bytes;
      if (c.prefixCount < sizeof(c.prefix)) c.prefix[c.prefixCount++] = byte;
      if (c.parser.feed(byte)) c.lastValidMs = millis();
    }
  }
  if (static_cast<uint32_t>(millis() - lastSummaryMs) >= 1000) printSummary();
  delay(1);
}
