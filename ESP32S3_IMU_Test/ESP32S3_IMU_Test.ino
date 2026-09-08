/*
  Standalone ESP32-S3 + STRDC BNO085, four-wire I2C test.
  ESP32-S3 3V3 -> IMU VDC; GND -> GND; GPIO8 -> SDA; GPIO9 -> SCL.
  Use GPIO numbers, not header positions. See README for USB settings.
  No robot connections, motor code, RST wire, or INT wire.
  Starts with accelerometer only. Send g to add calibrated gyro, ? for help.
  Unplug USB to power-cycle BOTH boards before repeating the comparison.
*/
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module (or your specific ESP32-S3 board)."
#endif

constexpr int SDA_PIN = 8;
constexpr int SCL_PIN = 9;
constexpr uint32_t REPORT_US = 20000;  // Request 50 Hz.
constexpr uint32_t STALE_MS = 1000;

Adafruit_BNO08x imu(-1);
bool ready = false;
bool resetSeen = false;
bool gyroRequested = false;
bool reportEnabled[2] = {false, false};
uint32_t samples[2] = {}, previousSamples[2] = {}, lastSampleMs[2] = {};
float xyz[2][3] = {};
uint32_t lastSummaryMs = 0;
uint32_t decodeErrors = 0;

// SH-2 calls this for EVERY report, including multiple reports in one packet.
// Adafruit getSensorEvent() exposes only the last report in a serviced packet.
void onSensorEvent(void *, sh2_SensorEvent_t *rawEvent) {
  sh2_SensorValue_t event = {};
  if (sh2_decodeSensorEvent(&event, rawEvent) != SH2_OK) {
    ++decodeErrors;
    return;
  }
  if (!ready || resetSeen) return;
  int slot = -1;
  if (event.sensorId == SH2_ACCELEROMETER) {
    slot = 0;
    xyz[0][0] = event.un.accelerometer.x;
    xyz[0][1] = event.un.accelerometer.y;
    xyz[0][2] = event.un.accelerometer.z;
  } else if (event.sensorId == SH2_GYROSCOPE_CALIBRATED && gyroRequested) {
    slot = 1;
    xyz[1][0] = event.un.gyroscope.x;
    xyz[1][1] = event.un.gyroscope.y;
    xyz[1][2] = event.un.gyroscope.z;
  }
  if (slot >= 0) {
    ++samples[slot];
    lastSampleMs[slot] = millis();
  }
}

void printHelp() {
  Serial.println("ESP32S3_IMU_TEST V1 | SDA8 SCL9 | I2C 100 kHz | USB serial 115200");
  Serial.println("Starts ACCEL only. Observe 30 seconds, then send g to add GYRO.");
  Serial.println("? = help. No automatic retries. Unplug USB to power-cycle both boards for a new test.");
}

void startGyro() {
  if (!ready || resetSeen) {
    Serial.println("Cannot start gyro: power-cycle both boards and check startup output.");
    return;
  }
  if (gyroRequested) {
    Serial.println("Gyro already requested; observing without restarting it.");
    return;
  }
  gyroRequested = true;
  Serial.printf("GYRO requested at ms=%lu\n", static_cast<unsigned long>(millis()));
  reportEnabled[1] = imu.enableReport(SH2_GYROSCOPE_CALIBRATED, REPORT_US);
  Serial.println(reportEnabled[1] ? "GYRO configuration sent; wait for fresh samples."
                                 : "GYRO configuration failed.");
}

void printSummary() {
  const uint32_t now = millis();
  Serial.printf("ms=%lu TEST=%s decode_errors=%lu\n", static_cast<unsigned long>(now),
                !ready ? "NOT_READY" : resetSeen ? "RESET_STOPPED" : gyroRequested ? "ACCEL_PLUS_GYRO" : "ACCEL_ONLY (send g)",
                static_cast<unsigned long>(decodeErrors));
  if (!ready || resetSeen) {
    Serial.println("Unplug USB from the S3 to remove power from BOTH boards, then reconnect.");
  }
  for (unsigned i = 0; i < 2; ++i) {
    const uint32_t age = now - lastSampleMs[i];
    const bool requested = i == 0 || gyroRequested;
    const char *state = !ready ? "NOT_READY" : resetSeen ? "RESET_STOPPED" : !requested ? "OFF" :
                        !reportEnabled[i] ? "ENABLE_FAILED" : !samples[i] ? "WAIT" : age > STALE_MS ? "STALE" : "LIVE";
    Serial.printf("%s %s samples=%lu new=%lu age_ms=%ld",
                  i == 0 ? "ACCEL" : "GYRO", state,
                  static_cast<unsigned long>(samples[i]),
                  static_cast<unsigned long>(samples[i] - previousSamples[i]),
                  samples[i] ? static_cast<long>(age) : -1L);
    if (samples[i]) {
      Serial.printf(" last_xyz=%.3f,%.3f,%.3f %s", xyz[i][0], xyz[i][1], xyz[i][2],
                    i == 0 ? "m/s^2" : "rad/s");
    }
    Serial.println();
    previousSamples[i] = samples[i];
  }
  lastSummaryMs = now;
}

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) delay(10);
  delay(500);  // Allow the IMU to boot after USB power is applied.
  printHelp();

  if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Serial.println("FAIL: I2C bus could not start.");
    return;
  }
  Wire.setTimeOut(50);
  uint8_t foundAddress = 0;
  for (uint8_t address = 0x4A; address <= 0x4B; ++address) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    Serial.printf("I2C 0x%02X: %s (code %u)\n", address, error == 0 ? "ACK" : "no ACK", error);
    if (!error && !foundAddress) foundAddress = address;
  }
  if (!foundAddress) {
    Serial.println("FAIL: no BNO085 address responded. Check the four wires and I2C mode.");
    return;
  }
  Serial.println("Initializing BNO085...");
  if (!imu.begin_I2C(foundAddress, &Wire)) {
    Serial.println("FAIL: address responded, but BNO085 initialization failed.");
    return;
  }
  if (sh2_setSensorCallback(onSensorEvent, nullptr) != SH2_OK) {
    Serial.println("FAIL: could not register sensor callback.");
    return;
  }
  // Consume the normal initialization reset notice before starting the test.
  sh2_service();
  imu.wasReset();
  ready = true;
  Serial.printf("BNO085 initialized at 0x%02X\n", foundAddress);
  reportEnabled[0] = imu.enableReport(SH2_ACCELEROMETER, REPORT_US);
  Serial.println(reportEnabled[0] ? "ACCEL configuration sent; wait for fresh samples."
                                 : "ACCEL configuration failed.");
  lastSummaryMs = millis();
}

void loop() {
  if (Serial.available()) {
    const int command = Serial.read();
    if (command == 'g' || command == 'G') startGyro();
    else if (command == '?') printHelp();
  }

  if (ready && !resetSeen) {
    sh2_service();
    if (imu.wasReset()) {
      resetSeen = true;
      Serial.println("IMU RESET DETECTED: test stopped. Power-cycle both boards before repeating.");
    }
  }
  if (millis() - lastSummaryMs >= 1000) printSummary();
  delay(1);
}
