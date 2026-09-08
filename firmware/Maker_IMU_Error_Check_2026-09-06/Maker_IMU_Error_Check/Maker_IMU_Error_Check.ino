// Automatic STRDC BNO085 error-log check for Maker ESP32 Pro.
// No motion commands, calibration writes, or automatic reset loop.
#include <Arduino.h>
#include <SPI.h>
#include "MakerBnoSpi.h"

constexpr uint8_t SCK_PIN = 22, MISO_PIN = 21, MOSI_PIN = 32;
constexpr uint8_t CS_PIN = 33, INT_PIN = 26, RST_PIN = 25, WAKE_PIN = 16;
constexpr uint8_t MOTOR_PINS[] = {17, 12, 14, 15, 4, 2, 27, 13};
MakerBnoSpi imu(RST_PIN, WAKE_PIN, INT_PIN);
bool initialized = false;
bool reportSent[3] = {};
bool featureSent = false, productSent = false, errorsSent = false;
uint32_t lastSummaryMs = 0;

void serviceFor(uint32_t durationMs) {
  const uint32_t started = millis();
  do {
    if (initialized) {
      sh2_SensorValue_t event;
      for (unsigned i = 0; i < 12 && imu.getSensorEvent(&event); ++i) {}
    }
    delay(1);
  } while (static_cast<uint32_t>(millis() - started) < durationMs);
}

bool quaternionInterval(uint32_t &interval) {
  // Latest complete FC08 prefix contains the complete configuration report.
  bool found = false;
  for (unsigned i = 0; i < imu.traceCount(); ++i) {
    const auto *p = imu.traceAt(i);
    if (!p || p->direction != 'R' || p->prefixLength < 21 ||
        p->prefix[2] != 2 || p->prefix[4] != 0xFC || p->prefix[5] != 0x08) continue;
    interval = static_cast<uint32_t>(p->prefix[9]) |
               (static_cast<uint32_t>(p->prefix[10]) << 8) |
               (static_cast<uint32_t>(p->prefix[11]) << 16) |
               (static_cast<uint32_t>(p->prefix[12]) << 24);
    found = true;
  }
  return found;
}

void printSummary() {
  Serial.println("CHECK RESULT BEGIN");
  Serial.println("FIRMWARE MAKER_IMU_ERROR_CHECK_V1");
  Serial.println("MOTORS DISABLED; SPI SCK22 MISO21 MOSI32 CS33 INT26 RST25 WAKE16");
  Serial.printf("STARTUP %s; INT %u\n", initialized ? "OK" : "FAILED", digitalRead(INT_PIN));
  Serial.printf("REPORT WRITES Q%u G%u A%u (transmitted, not acknowledged)\n",
                reportSent[0] ? 1U : 0U, reportSent[1] ? 1U : 0U, reportSent[2] ? 1U : 0U);
  Serial.printf("PROBES FEATURE %u PRODUCT %u ERRORS %u\n",
                featureSent ? 1U : 0U, productSent ? 1U : 0U, errorsSent ? 1U : 0U);
  uint32_t interval = 0;
  if (quaternionInterval(interval)) Serial.printf("QUATERNION INTERVAL_US %lu\n", static_cast<unsigned long>(interval));
  else Serial.println("QUATERNION CONFIG RESPONSE NOT CAPTURED");
  const auto &d = imu.diagnostics();
  Serial.printf("SENSOR EVENTS %lu; RX %lu TX %lu BAD %lu WAKE_TIMEOUT %lu\n",
                static_cast<unsigned long>(d.sensorEvents), static_cast<unsigned long>(d.rxPackets),
                static_cast<unsigned long>(d.txPackets), static_cast<unsigned long>(d.badHeaders),
                static_cast<unsigned long>(d.wakeTimeouts));
  Serial.printf("ERROR LOG COMPLETE %u RECORDS %u OVERFLOW %lu\n",
                imu.errorResponseComplete() ? 1U : 0U, imu.errorCount(),
                static_cast<unsigned long>(imu.errorOverflowCount()));
  Serial.println("ERROR SEVERITY THRESHOLD 0 (highest priority; does not check every severity)");
  for (unsigned i = 0; i < imu.errorCount(); ++i) {
    const auto *e = imu.errorAt(i);
    if (!e) break;
    Serial.printf("ERROR %u SEVERITY %u SEQUENCE %u SOURCE %u ERROR %u MODULE %u CODE %u\n",
                  i, e->severity, e->sequence, e->source, e->error, e->module, e->code);
  }
  if (!imu.errorResponseComplete()) Serial.println("ERROR LOG INCOMPLETE; missing reply is not a clean result");
  Serial.printf("TRACE COUNT %u OVERFLOW %lu\n", imu.traceCount(),
                static_cast<unsigned long>(imu.traceOverflowCount()));
  Serial.println("CHECK RESULT END");
  lastSummaryMs = millis();
}

void setup() {
  for (uint8_t pin : MOTOR_PINS) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  delay(500);
  Serial.println("FIRMWARE MAKER_IMU_ERROR_CHECK_V1; starting automatic check");
  digitalWrite(CS_PIN, HIGH);
  pinMode(CS_PIN, OUTPUT);
  if (SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN))
    initialized = imu.begin_SPI(CS_PIN, INT_PIN, &SPI);
  if (initialized) {
    imu.wasReset(); // Consume startup notification before requesting reports.
    reportSent[0] = imu.enableReport(SH2_GAME_ROTATION_VECTOR, 20000);
    serviceFor(250); // STRDC services incoming replies between feature requests.
    reportSent[1] = imu.enableReport(SH2_GYROSCOPE_CALIBRATED, 20000);
    serviceFor(250);
    reportSent[2] = imu.enableReport(SH2_LINEAR_ACCELERATION, 20000);
    serviceFor(500);
    // Raw queries advance the transport sequence. No later SH-2 control writes.
    featureSent = imu.probeControl(0xFE, 0x08);
    serviceFor(500);
    productSent = imu.probeControl(0xF9, 0x00);
    serviceFor(500);
    errorsSent = imu.probeErrors();
    serviceFor(1000);
  }
  printSummary();
}

void loop() {
  serviceFor(1);
  // Repeat the stored summary so opening Serial Monitor late loses nothing.
  // This does not reset the IMU or request the error log again.
  if (static_cast<uint32_t>(millis() - lastSummaryMs) >= 10000) printSummary();
}
