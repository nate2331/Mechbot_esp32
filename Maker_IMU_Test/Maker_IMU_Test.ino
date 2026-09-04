/*
  Maker ESP32 Pro + BNO085 four-wire I2C bench test.
  Arduino board: ESP32 Dev Module (not S3).
  Library: Adafruit BNO08x and its dependencies.
  SDA=21, SCL=22, common GND, power appropriate to your breakout.
  User confirms breakout accepts 5V input; SDA/SCL must remain 3.3V safe.
  No RST or INT wire. All eight motor-driver inputs stay LOW.
  Serial 115200: i=initialize/retry, ?=help, x=hold motors stopped.
  Reports relative yaw/pitch/roll using the game rotation vector (no compass).
  This is a standalone diagnostic, not the robot navigation firmware.
*/
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "Select ESP32 Dev Module for the Maker ESP32 Pro, not ESP32-S3."
#endif

constexpr uint8_t MOTOR_PINS[] = {27, 13, 4, 2, 17, 12, 14, 15};
constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;
constexpr uint32_t REPORT_US = 20000;  // Sensor reports at 50 Hz.
constexpr uint32_t PRINT_MS = 200;    // Serial output at 5 Hz.
constexpr uint32_t STALE_MS = 1000;
constexpr float TO_DEGREES = 57.2957795131F;
Adafruit_BNO08x imu(-1);              // No hardware reset connection.
sh2_SensorValue_t event;
bool imuReady = false;
bool reportsReady = false;
uint32_t lastSampleAt = 0;
uint32_t lastPrintAt = 0;
uint32_t lastWarningAt = 0;
uint32_t lastReportAttemptAt = 0;
uint32_t sampleCount = 0;

void holdMotorsStopped() {
  for (uint8_t pin : MOTOR_PINS) digitalWrite(pin, LOW);
}

void help() {
  Serial.println("MAKER_IMU_TEST V1 | SDA21 SCL22 | no RST/INT wire");
  Serial.println("All motors held stopped. i=initialize/retry, ?=help, x=STOP");
  Serial.println("Angles in degrees, sensor axes. Yaw is relative and may drift, NOT compass north.");
  Serial.println("Rotate/tilt the sensor slowly to check changing orientation.");
}

void enableReports() {
  lastReportAttemptAt = millis();
  reportsReady = imu.enableReport(SH2_GAME_ROTATION_VECTOR, REPORT_US);
  Serial.println(reportsReady ? "Game rotation vector enabled at 50 Hz."
                             : "WARN: report enable failed; will retry.");
}

void initializeImu() {
  holdMotorsStopped();
  imuReady = false;
  reportsReady = false;
  sampleCount = 0;
  Serial.println("Checking BNO08x addresses 0x4A and 0x4B...");
  for (uint8_t address : {0x4A, 0x4B}) {
    Wire.beginTransmission(address);
    const uint8_t error = Wire.endTransmission();
    Serial.printf("I2C 0x%02X: %s (code %u)\n", address,
                  error == 0 ? "ACK" : "no ACK", error);
    if (error != 0) continue;
    if (!imu.begin_I2C(address, &Wire)) {
      Serial.println("Address ACKed, but BNO08x initialization failed.");
      continue;
    }
    imuReady = true;
    Serial.printf("BNO08x initialized at 0x%02X\n", address);
    for (uint8_t i = 0; i < imu.prodIds.numEntries; ++i) {
      const auto &id = imu.prodIds.entry[i];
      Serial.printf("Part %lu firmware %u.%u.%u build %lu\n",
                    static_cast<unsigned long>(id.swPartNumber),
                    id.swVersionMajor, id.swVersionMinor, id.swVersionPatch,
                    static_cast<unsigned long>(id.swBuildNumber));
    }
    enableReports();
    lastSampleAt = lastPrintAt = lastWarningAt = millis();
    return;
  }
  Serial.println("IMU NOT READY. Check SDA/SCL, GND, power and I2C mode; then send i.");
  Serial.println("If needed power-cycle the sensor; no hardware-reset wire is connected.");
}

void setup() {
  // Do this before serial/I2C initialization. No PWM is attached in this sketch.
  for (uint8_t pin : MOTOR_PINS) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  holdMotorsStopped();
  Serial.begin(115200);
  delay(300);
  help();
  if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Serial.println("FATAL: I2C bus setup failed. Reset board after checking configuration.");
    return;
  }
  Wire.setTimeOut(50);
  initializeImu();
}

void loop() {
  holdMotorsStopped();
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'i' || command == 'I') initializeImu();
    else if (command == '?') help();
    else if (command == 'x' || command == 'X') Serial.println("STOP: all motor outputs LOW.");
    else if (command != '\r' && command != '\n' && command != ' ')
      Serial.println("No motor commands in this sketch. Send ? for help.");
  }
  if (!imuReady) { delay(1); return; }
  if (imu.wasReset()) {
    Serial.println("WARN: sensor reset detected; re-enabling reports.");
    enableReports();
    lastSampleAt = millis();
  }
  if (!reportsReady && millis() - lastReportAttemptAt >= 2000) enableReports();
  if (imu.getSensorEvent(&event) && event.sensorId == SH2_GAME_ROTATION_VECTOR) {
    float w = event.un.gameRotationVector.real;
    float x = event.un.gameRotationVector.i;
    float y = event.un.gameRotationVector.j;
    float z = event.un.gameRotationVector.k;
    const float norm = sqrtf(w*w + x*x + y*y + z*z);
    if (isfinite(norm) && norm > 0.0001F) {
      w /= norm; x /= norm; y /= norm; z /= norm;
      lastSampleAt = millis();
      ++sampleCount;
      if (lastSampleAt - lastPrintAt >= PRINT_MS) {
        const float sinPitch = fmaxf(-1.0F, fminf(1.0F, 2.0F*(w*y - z*x)));
        const float yaw = atan2f(2.0F*(w*z + x*y), 1.0F - 2.0F*(y*y + z*z)) * TO_DEGREES;
        const float pitch = asinf(sinPitch) * TO_DEGREES;
        const float roll = atan2f(2.0F*(w*x + y*z), 1.0F - 2.0F*(x*x + y*y)) * TO_DEGREES;
        Serial.printf("IMU ms=%lu samples=%lu yaw=%.1f pitch=%.1f roll=%.1f status=%u q=%.4f,%.4f,%.4f,%.4f\n",
                      static_cast<unsigned long>(lastSampleAt), static_cast<unsigned long>(sampleCount),
                      yaw, pitch, roll, event.status, x, y, z, w);
        lastPrintAt = lastSampleAt;
      }
    }
  }
  const uint32_t now = millis();
  if (now - lastSampleAt > STALE_MS && now - lastWarningAt >= 2000) {
    Serial.println("WARN: no fresh orientation for >1s. Check wiring; send i to retry.");
    lastWarningAt = now;
  }
  delay(1);
}
