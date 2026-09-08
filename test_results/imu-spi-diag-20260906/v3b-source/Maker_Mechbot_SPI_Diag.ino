/*
  Temporary Maker ESP32 Pro SPI BNO085 packet diagnostic; all motion disabled.
  M2=FL, M3=FR, M1=RL, M0=RR. ESP32 Dev Module; Arduino ESP32 core 3.x.
  Game rotation vector: relative yaw, not magnetic north. RST25 INT26 WAKE16.
  SPI: SCK22 MISO21 MOSI32 CS33; STRDC P1 to 3.3 V, P0 to WAKE16.
  Fresh defaults: all PWM ceilings 177; heading correction and field mode OFF.
  Encoders are diagnostic/open-loop feedback, not wheel-speed PID.
  Keep wheels raised for initial combined tests; no current/stall protection.

  Serial protocol at 115200 baud:
    V <forward> <left> <ccw>   Rejected; motors remain stopped
    IMU TRACE                 Dump bounded startup packet capture
    IMU RETRY                 Manually reset and restart capture
    F <0|1>                    Disable/enable field-oriented translation
    Z                          Re-zero the current field heading
    X                         Immediate stop
    ?                         Print help

  Telemetry:
    T <ms> <FL> <FR> <RL> <RR>
    I <ms> <qx> <qy> <qz> <qw> <gx> <gy> <gz> <ax> <ay> <az> <status>

  Quaternion order matches ROS: x, y, z, w.
  Gyroscope units are rad/s. Linear acceleration units are m/s^2.
  IMU status is 0-3, where 3 is highest accuracy.

  Safety:
    - Motors remain disabled; motion commands are rejected.
    - A timeout or malformed motion command stops all motors.
    - No automatic IMU retries or host-initiated hardware resets after startup.
    - Heading hold is disabled by default and yields to deliberate turn input.
    - Field-oriented control defaults to disabled.
    - Field-oriented motion stops if its required heading becomes unavailable.
*/

#include <Arduino.h>
#include <SPI.h>
#include "MakerBnoSpi.h"
#include <Preferences.h>
#include <esp_arduino_version.h>

#include "NavigationMath.h"
#include "MotorSafety.h"
#include "EncoderSampling.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#if !defined(CONFIG_IDF_TARGET_ESP32) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Select ESP32 Dev Module and Espressif Arduino core 3.x or newer."
#endif

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr size_t SERIAL_TX_BUFFER_SIZE = 2048;
constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint32_t COMMAND_TIMEOUT_MS = 300;
static_assert(COMMAND_TIMEOUT_MS == MotorSafety::TIMEOUT_MS, "Watchdog deadlines must match");
constexpr uint32_t TELEMETRY_INTERVAL_MS = 200;
// Encoder CPR intentionally uncalibrated; report raw x4 counts only.
constexpr size_t RX_BUFFER_SIZE = 128;

// Explicit remapping avoids the default SPI pins used by wheel encoders.
constexpr int IMU_SCK_PIN = 22;
constexpr int IMU_MISO_PIN = 21;
constexpr int IMU_MOSI_PIN = 32;
constexpr int IMU_CS_PIN = 33;
constexpr int IMU_INT_PIN = 26;
constexpr int IMU_RESET_PIN = 25;
constexpr int IMU_WAKE_PIN = 16;
constexpr uint32_t IMU_REPORT_INTERVAL_US = 20000;  // 50 Hz
constexpr uint32_t IMU_STALE_MS = 500;
constexpr uint32_t IMU_RETRY_INTERVAL_MS = 2000;
constexpr uint32_t IMU_REPORT_RETRY_INTERVAL_MS = 1000;
constexpr uint32_t IMU_REPORT_RECOVERY_GRACE_MS = 2000;
constexpr uint32_t IMU_REPORT_RECOVERY_RESET_MS = 3000;
// Game rotation vector provides relative yaw, not magnetic north. Report status
// is only a quality gate; field mode is relative to the user-captured zero.
constexpr uint8_t IMU_MIN_HEADING_HOLD_STATUS = 0;
constexpr uint8_t IMU_MIN_FIELD_ORIENTED_STATUS = 1;

// Starting values only. Tune on blocks at low speed before unrestricted use.
constexpr float DEFAULT_HEADING_HOLD_KP = 0.70F;
constexpr float DEFAULT_HEADING_HOLD_MAX_CORRECTION = 0.30F;
constexpr float DEFAULT_HEADING_HOLD_ERROR_DEADBAND_RAD =
    1.5F * NavigationMath::PI_F / 180.0F;
constexpr float MANUAL_TURN_DEADBAND = 0.01F;
constexpr float TRANSLATION_DEADBAND = 0.01F;

enum MotorIndex : uint8_t {
  FRONT_LEFT = 0,
  FRONT_RIGHT,
  REAR_LEFT,
  REAR_RIGHT,
  MOTOR_COUNT
};

struct MotorConfig {
  const char *name;
  uint8_t dir1Pin;
  uint8_t dir2Pin;
  uint8_t encoderAPin;
  uint8_t encoderBPin;
  uint8_t matchedPwm;
  int8_t motorPolarity;
  int8_t encoderPolarity;
};

MotorConfig motors[MOTOR_COUNT] = {
  {"FL", 17, 12, 35, 36, 177, +1, +1},
  {"FR", 14, 15, 34, 39, 177, +1, -1},
  {"RL",  4,  2,  5, 23, 177, +1, +1},
  {"RR", 27, 13, 18, 19, 177, -1, -1}
};

volatile int64_t encoderCounts[MOTOR_COUNT] = {0, 0, 0, 0};
volatile uint8_t previousEncoderState[MOTOR_COUNT] = {0, 0, 0, 0};
volatile uint32_t encoderAEdges[MOTOR_COUNT] = {};
volatile uint32_t encoderBEdges[MOTOR_COUNT] = {};
volatile uint32_t encoderInvalid[MOTOR_COUNT] = {};
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;

DRAM_ATTR const int8_t quadratureDelta[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};



MakerBnoSpi bno08x(IMU_RESET_PIN, IMU_WAKE_PIN, IMU_INT_PIN);
Preferences preferences;
sh2_SensorValue_t imuEvent;

struct RuntimeSettings {
  uint8_t pwm[MOTOR_COUNT];
  float headingKp;
  float headingMax;
  float headingDeadbandRad;
  int8_t headingSign;
  bool headingEnabled;
};

RuntimeSettings settings = {{177, 177, 177, 177},
  DEFAULT_HEADING_HOLD_KP, DEFAULT_HEADING_HOLD_MAX_CORRECTION,
  DEFAULT_HEADING_HOLD_ERROR_DEADBAND_RAD, +1, false};

bool imuAvailable = false;
bool imuQuaternionValid = false;
bool imuGyroValid = false;
bool imuAccelerationValid = false;
bool imuInitializationAttempted = false;
uint8_t imuStatus = 0;
uint32_t lastImuEventMs = 0;
uint32_t lastImuQuaternionMs = 0;
uint32_t lastImuGyroMs = 0;
uint32_t lastImuAccelerationMs = 0;
uint32_t lastImuInitAttemptMs = 0;
uint32_t lastImuReportRetryMs = 0;
uint32_t imuResetCount = 0;
uint32_t imuReinitCount = 0;
uint32_t imuReportRetryCount = 0;
bool imuReportsUnhealthy = false;
bool imuReportsRetried = false;
uint32_t imuReportsUnhealthySinceMs = 0;
uint32_t imuReportsFirstRetryMs = 0;

float imuQx = 0.0F;
float imuQy = 0.0F;
float imuQz = 0.0F;
float imuQw = 1.0F;
float imuGx = 0.0F;
float imuGy = 0.0F;
float imuGz = 0.0F;
float imuAx = 0.0F;
float imuAy = 0.0F;
float imuAz = 0.0F;

char rxBuffer[RX_BUFFER_SIZE];
size_t rxLength = 0;
bool rxDiscardLine = false;
uint32_t lastCommandMs = 0;
uint32_t lastTelemetryMs = 0;
bool commandActive = false;
bool motionRequested = false;
bool watchdogReported = false;

bool headingTargetValid = false;
bool fieldOrientedEnabled = false;
bool fieldReferenceValid = false;
bool navigationImuFaultReported = false;
float headingTargetYaw = 0.0F;
float fieldReferenceYaw = 0.0F;
float lastHeadingError = 0.0F;
float lastHeadingCorrection = 0.0F;

void applySettingsToMotors() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) motors[i].matchedPwm = settings.pwm[i];
}

void resetSettingsToDefaults() {
  const uint8_t defaults[MOTOR_COUNT] = {177, 177, 177, 177};
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) settings.pwm[i] = defaults[i];
  settings.headingKp = DEFAULT_HEADING_HOLD_KP;
  settings.headingMax = DEFAULT_HEADING_HOLD_MAX_CORRECTION;
  settings.headingDeadbandRad = DEFAULT_HEADING_HOLD_ERROR_DEADBAND_RAD;
  settings.headingSign = +1;
  settings.headingEnabled = false;
  applySettingsToMotors();
}

void loadSettings() {
  resetSettingsToDefaults();
  if (!preferences.begin("maker_v1", true)) return;
  settings.pwm[0] = preferences.getUChar("pwm_fl", settings.pwm[0]);
  settings.pwm[1] = preferences.getUChar("pwm_fr", settings.pwm[1]);
  settings.pwm[2] = preferences.getUChar("pwm_rl", settings.pwm[2]);
  settings.pwm[3] = preferences.getUChar("pwm_rr", settings.pwm[3]);
  settings.headingKp = preferences.getFloat("head_kp", settings.headingKp);
  settings.headingMax = preferences.getFloat("head_max", settings.headingMax);
  settings.headingDeadbandRad = preferences.getFloat("head_db", settings.headingDeadbandRad);
  settings.headingSign = preferences.getChar("head_sign", settings.headingSign);
  settings.headingEnabled = preferences.getBool("head_on", settings.headingEnabled);
  preferences.end();
  if (!isfinite(settings.headingKp) || !isfinite(settings.headingMax) ||
      !isfinite(settings.headingDeadbandRad) || settings.headingKp < 0 || settings.headingKp > 5 || settings.headingMax < 0 ||
      settings.headingMax > 1 || settings.headingDeadbandRad < 0 ||
      settings.headingDeadbandRad > NavigationMath::PI_F / 6.0F ||
      (settings.headingSign != -1 && settings.headingSign != 1)) {
    Serial.println("WARN invalid saved settings; defaults restored");
    resetSettingsToDefaults();
  }
  applySettingsToMotors();
}

bool saveSettings() {
  if (!preferences.begin("maker_v1", false)) return false;
  bool ok = true;
  ok &= preferences.putUChar("pwm_fl", settings.pwm[0]) == 1;
  ok &= preferences.putUChar("pwm_fr", settings.pwm[1]) == 1;
  ok &= preferences.putUChar("pwm_rl", settings.pwm[2]) == 1;
  ok &= preferences.putUChar("pwm_rr", settings.pwm[3]) == 1;
  ok &= preferences.putFloat("head_kp", settings.headingKp) == sizeof(float);
  ok &= preferences.putFloat("head_max", settings.headingMax) == sizeof(float);
  ok &= preferences.putFloat("head_db", settings.headingDeadbandRad) == sizeof(float);
  ok &= preferences.putChar("head_sign", settings.headingSign) == 1;
  ok &= preferences.putBool("head_on", settings.headingEnabled) == 1;
  preferences.end();
  return ok;
}

void printSettings() {
  Serial.printf("CFG pwm-fl %u\nCFG pwm-fr %u\nCFG pwm-rl %u\nCFG pwm-rr %u\n",
                settings.pwm[0], settings.pwm[1], settings.pwm[2], settings.pwm[3]);
  Serial.printf("CFG heading-kp %.4f\nCFG heading-max %.4f\n", settings.headingKp, settings.headingMax);
  Serial.printf("CFG heading-deadband-deg %.3f\n", settings.headingDeadbandRad * 180.0F / NavigationMath::PI_F);
  Serial.printf("CFG heading-sign %d\nCFG heading-enabled %u\n", settings.headingSign, settings.headingEnabled ? 1U : 0U);
  Serial.println("OK CFG GET");
}

void IRAM_ATTR updateEncoder(uint8_t index) {
  portENTER_CRITICAL_ISR(&encoderMux);
  const uint8_t currentState = sampleEncoderState(motors[index].encoderAPin, motors[index].encoderBPin);
  const uint8_t changed = previousEncoderState[index] ^ currentState;
  if (changed & 2) ++encoderAEdges[index];
  if (changed & 1) ++encoderBEdges[index];
  if (changed == 3) ++encoderInvalid[index];
  const uint8_t transition = (previousEncoderState[index] << 2) | currentState;
  previousEncoderState[index] = currentState;

  encoderCounts[index] += quadratureDelta[transition] * motors[index].encoderPolarity;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

void IRAM_ATTR frontLeftEncoderISR()  { updateEncoder(FRONT_LEFT); }
void IRAM_ATTR frontRightEncoderISR() { updateEncoder(FRONT_RIGHT); }
void IRAM_ATTR rearLeftEncoderISR()   { updateEncoder(REAR_LEFT); }
void IRAM_ATTR rearRightEncoderISR()  { updateEncoder(REAR_RIGHT); }

// Only the output task and immediate stop write PWM. Mutex never spans IMU/Serial IO.
SemaphoreHandle_t motorMutex = nullptr;
MotorSafety::Controller outputController;
bool motorAttached[MOTOR_COUNT][2] = {};
float pendingMotorCommands[MOTOR_COUNT] = {};
bool motorWatchdogTripped = false;

void writeMotorDuty(uint8_t index, float command) {
  const float electrical = command * motors[index].motorPolarity;
  const uint8_t duty = static_cast<uint8_t>(lroundf(fabsf(electrical)));
  if (electrical > 0) {
    ledcWrite(motors[index].dir2Pin, 0);
    ledcWrite(motors[index].dir1Pin, duty);
  } else {
    ledcWrite(motors[index].dir1Pin, 0);
    ledcWrite(motors[index].dir2Pin, duty);
  }
}

void zeroMotorOutputs() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (motorAttached[i][0]) ledcWrite(motors[i].dir1Pin, 0);
    else digitalWrite(motors[i].dir1Pin, LOW);
    if (motorAttached[i][1]) ledcWrite(motors[i].dir2Pin, 0);
    else digitalWrite(motors[i].dir2Pin, LOW);
  }
}

void motorOutputTask(void *) {
  TickType_t wake = xTaskGetTickCount();
  while (true) {
    xSemaphoreTake(motorMutex, portMAX_DELAY);
    if (outputController.tick(millis())) motorWatchdogTripped = true;
    for (uint8_t i = 0; i < MOTOR_COUNT; ++i)
      writeMotorDuty(i, outputController.wheels[i].applied);
    xSemaphoreGive(motorMutex);
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
  }
}

bool attachPwm(uint8_t index) {
  motorAttached[index][0] = ledcAttach(motors[index].dir1Pin, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  motorAttached[index][1] = ledcAttach(motors[index].dir2Pin, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  zeroMotorOutputs();
  return motorAttached[index][0] && motorAttached[index][1];
}

void stopAllMotors() {
  if (motorMutex) xSemaphoreTake(motorMutex, portMAX_DELAY);
  outputController.stop(millis());
  zeroMotorOutputs();
  if (motorMutex) xSemaphoreGive(motorMutex);
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) pendingMotorCommands[i] = 0;
  commandActive = false;
  motionRequested = false;
  headingTargetValid = false;
  lastHeadingError = 0.0F;
  lastHeadingCorrection = 0.0F;
}

void setMotorCommand(uint8_t index, float normalizedCommand) {
  normalizedCommand = constrain(normalizedCommand, -1.0F, 1.0F);
  pendingMotorCommands[index] = fabsf(normalizedCommand) < 0.01F
      ? 0.0F : normalizedCommand * motors[index].matchedPwm;
}

void publishMotorCommands() {
  xSemaphoreTake(motorMutex, portMAX_DELAY);
  outputController.publish(pendingMotorCommands, millis());
  motorWatchdogTripped = false;
  xSemaphoreGive(motorMutex);
}

bool readCurrentYaw(
    float &yaw,
    uint8_t minimumStatus = IMU_MIN_HEADING_HOLD_STATUS) {
  const uint32_t nowMs = millis();
  if (!imuAvailable || !imuQuaternionValid ||
      imuStatus < minimumStatus ||
      nowMs - lastImuQuaternionMs > IMU_STALE_MS) {
    return false;
  }

  return NavigationMath::quaternionToYaw(
      imuQx, imuQy, imuQz, imuQw, yaw);
}

void invalidateNavigationReferences(const char *reason) {
  const bool fieldWasEnabled = fieldOrientedEnabled;
  headingTargetValid = false;
  fieldOrientedEnabled = false;
  fieldReferenceValid = false;
  lastHeadingError = 0.0F;
  lastHeadingCorrection = 0.0F;
  if (fieldWasEnabled) {
    Serial.printf("WARN field-oriented control disabled: %s\n", reason);
  }
}

void applyVelocity(float, float, float) {
  // This temporary sensor diagnostic must never actuate the robot.
  stopAllMotors();
  Serial.println("ERR motion disabled in packet diagnostic firmware");
}

int64_t getEncoderCount(uint8_t index) {
  portENTER_CRITICAL(&encoderMux);
  const int64_t count = encoderCounts[index];
  portEXIT_CRITICAL(&encoderMux);
  return count;
}

void initializeImu();

bool enableImuReports() {
  bool ok = true;
  if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR,
                            IMU_REPORT_INTERVAL_US)) {
    Serial.println("WARN IMU rotation vector unavailable");
    ok = false;
  }
  delay(10);
  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED,
                            IMU_REPORT_INTERVAL_US)) {
    Serial.println("WARN IMU calibrated gyroscope unavailable");
    ok = false;
  }
  delay(10);
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION,
                            IMU_REPORT_INTERVAL_US)) {
    Serial.println("WARN IMU linear acceleration unavailable");
    ok = false;
  }
  delay(10);
  lastImuReportRetryMs = millis();
  imuReportsUnhealthy = true;
  imuReportsRetried = false;
  imuReportsUnhealthySinceMs = millis();
  return ok;
}

void retryMissingImuReports() {
  // Preserve this boot's packets and leave RST high until manual IMU RETRY.
}

void initializeImu() {
  stopAllMotors();
  invalidateNavigationReferences("IMU initialization");
  imuAvailable = false;
  imuQuaternionValid = false;
  imuGyroValid = false;
  imuAccelerationValid = false;
  imuReportsUnhealthy = false;
  imuReportsRetried = false;
  lastImuEventMs = 0;
  lastImuQuaternionMs = 0;
  lastImuGyroMs = 0;
  lastImuAccelerationMs = 0;
  lastImuInitAttemptMs = millis();
  ++imuReinitCount;
  if (imuInitializationAttempted) {
    // The adapter owns SH-2 session cleanup and only closes known-open state.
    // An unconditional sh2_close() can dereference null after failed startup.
    SPI.end();
  }
  imuInitializationAttempted = true;
  digitalWrite(IMU_CS_PIN, HIGH);
  pinMode(IMU_CS_PIN, OUTPUT);
  if (!SPI.begin(IMU_SCK_PIN, IMU_MISO_PIN, IMU_MOSI_PIN, IMU_CS_PIN)) {
    Serial.println("WARN IMU SPI bus setup failed; automatic retry disabled");
    return;
  }
  // The adapter keeps P0 high through the library reset/first interrupt and
  // implements the separate WAKE/INT handshake for subsequent host writes.
  if (!bno08x.begin_SPI(IMU_CS_PIN, IMU_INT_PIN, &SPI)) {
    Serial.printf("WARN IMU SPI startup failed; INT=%u; check SPI wiring and P1 high\n",
                  digitalRead(IMU_INT_PIN));
    return;
  }

  imuAvailable = true;
  if (enableImuReports()) {
    Serial.println("IMU SPI reports requested; waiting for fresh orientation, gyro and acceleration");
  } else {
    Serial.println("WARN IMU detected but one or more reports failed");
  }
}

void pollImu() {
  if (!imuAvailable) {
    // Diagnostic: retry only when the operator sends IMU RETRY.
    return;
  }

  if (bno08x.wasReset()) {
    stopAllMotors(); // Stop before report reconfiguration can block.
    ++imuResetCount;
    imuQuaternionValid = false;
    imuGyroValid = false;
    imuAccelerationValid = false;
    lastImuEventMs = 0;
    lastImuQuaternionMs = 0;
    lastImuGyroMs = 0;
    lastImuAccelerationMs = 0;
    invalidateNavigationReferences("IMU reset");
    Serial.println("WARN IMU reset observed; automatic report reconfiguration disabled");
  }

  // Bound the work per loop so IMU traffic cannot starve serial commands.
  for (uint8_t eventsRead = 0; eventsRead < 12; ++eventsRead) {
    if (!bno08x.getSensorEvent(&imuEvent)) {
      break;
    }

    switch (imuEvent.sensorId) {
      case SH2_GAME_ROTATION_VECTOR: {
        imuQx = imuEvent.un.gameRotationVector.i;
        imuQy = imuEvent.un.gameRotationVector.j;
        imuQz = imuEvent.un.gameRotationVector.k;
        imuQw = imuEvent.un.gameRotationVector.real;
        float checkedYaw = 0;
        imuQuaternionValid = NavigationMath::quaternionToYaw(imuQx, imuQy, imuQz, imuQw, checkedYaw);
        imuStatus = imuEvent.status;
        lastImuEventMs = bno08x.lastEventReceivedMs();
        lastImuQuaternionMs = lastImuEventMs;
        break;
      }
      case SH2_GYROSCOPE_CALIBRATED:
        imuGx = imuEvent.un.gyroscope.x;
        imuGy = imuEvent.un.gyroscope.y;
        imuGz = imuEvent.un.gyroscope.z;
        imuGyroValid = isfinite(imuGx) && isfinite(imuGy) && isfinite(imuGz);
        lastImuEventMs = bno08x.lastEventReceivedMs();
        lastImuGyroMs = lastImuEventMs;
        break;
      case SH2_LINEAR_ACCELERATION:
        imuAx = imuEvent.un.linearAcceleration.x;
        imuAy = imuEvent.un.linearAcceleration.y;
        imuAz = imuEvent.un.linearAcceleration.z;
        imuAccelerationValid = isfinite(imuAx) && isfinite(imuAy) && isfinite(imuAz);
        lastImuEventMs = bno08x.lastEventReceivedMs();
        lastImuAccelerationMs = lastImuEventMs;
        break;

      default:
        break;
    }
  }

  retryMissingImuReports();
}

void sendEncoderTelemetry() {
  Serial.printf("T %lu %lld %lld %lld %lld\n",
                static_cast<unsigned long>(millis()),
                static_cast<long long>(getEncoderCount(FRONT_LEFT)),
                static_cast<long long>(getEncoderCount(FRONT_RIGHT)),
                static_cast<long long>(getEncoderCount(REAR_LEFT)),
                static_cast<long long>(getEncoderCount(REAR_RIGHT)));
}

void sendImuTelemetry() {
  const uint32_t nowMs = millis();

  if (!imuAvailable) {
    Serial.printf("I %lu OFFLINE\n", static_cast<unsigned long>(nowMs));
    return;
  }

  if (!imuQuaternionValid || !imuGyroValid || !imuAccelerationValid) {
    Serial.printf("I %lu WAIT Q%u G%u A%u\n",
                  static_cast<unsigned long>(nowMs),
                  imuQuaternionValid ? 1U : 0U,
                  imuGyroValid ? 1U : 0U,
                  imuAccelerationValid ? 1U : 0U);
    return;
  }

  if (nowMs - lastImuQuaternionMs > IMU_STALE_MS ||
      nowMs - lastImuGyroMs > IMU_STALE_MS || nowMs - lastImuAccelerationMs > IMU_STALE_MS) {
    Serial.printf("I %lu STALE\n", static_cast<unsigned long>(nowMs));
    return;
  }

  Serial.printf(
      "I %lu %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %u\n",
      static_cast<unsigned long>(nowMs),
      imuQx, imuQy, imuQz, imuQw,
      imuGx, imuGy, imuGz,
      imuAx, imuAy, imuAz,
      static_cast<unsigned int>(imuStatus));
}

void sendNavigationTelemetry() {
  const uint32_t nowMs = millis();
  float currentYaw = 0.0F;
  const bool headingAvailable = readCurrentYaw(currentYaw);
  Serial.printf("N %lu %.6f %.6f %.6f %.4f %u %u %u\n",
                static_cast<unsigned long>(nowMs),
                headingAvailable ? currentYaw : 0.0F,
                headingTargetValid ? headingTargetYaw : 0.0F,
                lastHeadingError,
                lastHeadingCorrection,
                settings.headingEnabled ? 1U : 0U,
                fieldOrientedEnabled ? 1U : 0U,
                headingAvailable ? 1U : 0U);
}

void sendImuDiagnostics() {
  // No report requests or hardware resets: this snapshot leaves outputs alone.
  const uint32_t nowMs = millis();
  Serial.printf("IMU DIAG SPI AVAILABLE %u INT %u Q %u AGE %lld G %u AGE %lld A %u AGE %lld RESET %lu REINIT %lu REPORT_RETRY %lu\n",
                imuAvailable ? 1U : 0U, digitalRead(IMU_INT_PIN),
                imuQuaternionValid ? 1U : 0U,
                imuQuaternionValid ? static_cast<long long>(uint32_t(nowMs - lastImuQuaternionMs)) : -1LL,
                imuGyroValid ? 1U : 0U,
                imuGyroValid ? static_cast<long long>(uint32_t(nowMs - lastImuGyroMs)) : -1LL,
                imuAccelerationValid ? 1U : 0U,
                imuAccelerationValid ? static_cast<long long>(uint32_t(nowMs - lastImuAccelerationMs)) : -1LL,
                static_cast<unsigned long>(imuResetCount),
                static_cast<unsigned long>(imuReinitCount),
                static_cast<unsigned long>(imuReportRetryCount));
  const auto& transport = bno08x.diagnostics();
  Serial.printf("IMU DIAG TRANSPORT RX %lu TX %lu NULL %lu BAD %lu WAKE_TIMEOUT %lu RX_FULL %lu RX_SMALL %lu EVENTS %lu DECODE_ERR %lu EVENT_DROP %lu RX_PENDING %u EVENT_PENDING %u\n",
                static_cast<unsigned long>(transport.rxPackets),
                static_cast<unsigned long>(transport.txPackets),
                static_cast<unsigned long>(transport.nullHeaders),
                static_cast<unsigned long>(transport.badHeaders),
                static_cast<unsigned long>(transport.wakeTimeouts),
                static_cast<unsigned long>(transport.rxQueueFull),
                static_cast<unsigned long>(transport.rxBufferTooSmall),
                static_cast<unsigned long>(transport.sensorEvents),
                static_cast<unsigned long>(transport.sensorDecodeErrors),
                static_cast<unsigned long>(transport.sensorQueueDrops),
                static_cast<unsigned int>(bno08x.queuedPackets()),
                static_cast<unsigned int>(bno08x.queuedSensorEvents()));
}


void sendDiagnostics() {
  float outputs[MOTOR_COUNT];
  uint32_t duty[MOTOR_COUNT][2], frequency[MOTOR_COUNT][2];
  xSemaphoreTake(motorMutex, portMAX_DELAY);
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    outputs[i] = outputController.wheels[i].applied;
    duty[i][0] = ledcRead(motors[i].dir1Pin);
    duty[i][1] = ledcRead(motors[i].dir2Pin);
    frequency[i][0] = ledcReadFreq(motors[i].dir1Pin);
    frequency[i][1] = ledcReadFreq(motors[i].dir2Pin);
  }
  xSemaphoreGive(motorMutex);
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    portENTER_CRITICAL(&encoderMux);
    const uint32_t a = encoderAEdges[i], b = encoderBEdges[i], bad = encoderInvalid[i];
    portEXIT_CRITICAL(&encoderMux);
    Serial.printf("D %s PWM %.1f A %lu B %lu INVALID %lu\n", motors[i].name, outputs[i],
      static_cast<unsigned long>(a), static_cast<unsigned long>(b), static_cast<unsigned long>(bad));
  }
  // Separate records preserve existing D parsers. These are peripheral-register
  // readbacks, not a measurement of GPIO waveforms or motor supply voltage.
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    Serial.printf("P %s PIN %u DUTY %lu HZ %lu PIN %u DUTY %lu HZ %lu\n",
      motors[i].name, motors[i].dir1Pin,
      static_cast<unsigned long>(duty[i][0]), static_cast<unsigned long>(frequency[i][0]),
      motors[i].dir2Pin,
      static_cast<unsigned long>(duty[i][1]), static_cast<unsigned long>(frequency[i][1]));
  }
}

void sendTelemetry() {
  sendEncoderTelemetry();
  sendImuTelemetry();
  sendNavigationTelemetry();
  Serial.printf("H %lu IMU %u %lu %lu %lu\n",
                static_cast<unsigned long>(millis()), imuAvailable ? 1U : 0U,
                static_cast<unsigned long>(lastImuQuaternionMs),
                static_cast<unsigned long>(imuResetCount),
                static_cast<unsigned long>(imuReinitCount));
}

void printHelp() {
  Serial.println("FIRMWARE MAKER_SPI_V3B_CONTROL_PROBE");
  Serial.println("DIAGNOSTIC: motion disabled; automatic report retries and hardware resets OFF");
  Serial.println("Commands:");
  Serial.println("  V                         rejected; motors remain stopped");
  Serial.println("  F <0|1>                    field-oriented control off/on");
  Serial.println("  Z                          re-zero field heading");
  Serial.println("  X                         immediate stop");
  Serial.println("  CFG GET|SAVE|RESET        runtime settings");
  Serial.println("  CFG SET <key> <value>     validated update while stopped");
  Serial.println("  ?                         help");
  Serial.printf("Heading hold: %s (verify sensor axes before enabling)\n", settings.headingEnabled ? "ON" : "OFF");
  Serial.println("Maker mapping: FL=M2 FR=M3 RL=M1 RR=M0; all encoders forward-positive");
  Serial.printf("IMU: SPI SCK%d MISO%d MOSI%d CS%d INT%d RST%d WAKE%d; relative yaw, NOT compass north\n",
                IMU_SCK_PIN, IMU_MISO_PIN, IMU_MOSI_PIN, IMU_CS_PIN,
                IMU_INT_PIN, IMU_RESET_PIN, IMU_WAKE_PIN);
  Serial.println("Default PWM=177 all wheels; independent output watchdog, ramped drive");
  Serial.println("  DIAG                      encoder edges/errors and applied PWM");
  Serial.println("  IMU RETRY                 stopped reinitialization");
  Serial.println("  IMU DIAG                  read-only stream ages (ms), INT and recovery counters; stop first");
  Serial.println("  IMU TRACE                 first 48 TX/RX packets, up to 32 bytes each");
  Serial.println("  IMU PROBE                 read feature configuration and product ID; bounded waits");
  Serial.println("Watchdog: 300 ms");
  Serial.println("Encoder: T <ms> <FL> <FR> <RL> <RR>");
  Serial.println("IMU: I <ms> <qx> <qy> <qz> <qw> <gx> <gy> <gz> <ax> <ay> <az> <status>");
  Serial.println("Navigation: N <ms> <yaw> <target> <error> <correction> <hold> <field> <ready>");
}

void sendImuTrace() {
  Serial.printf("IMU TRACE BEGIN COUNT %u OVERFLOW %lu\n", bno08x.traceCount(),
                static_cast<unsigned long>(bno08x.traceOverflowCount()));
  for (unsigned i = 0; i < bno08x.traceCount(); ++i) {
    const MakerBnoSpi::TraceEntry *entry = bno08x.traceAt(i);
    if (!entry) break;
    Serial.printf("PKT %u %c %lu LEN %u BYTES %u", i, entry->direction,
                  static_cast<unsigned long>(entry->timestampMs),
                  static_cast<unsigned>(entry->length),
                  static_cast<unsigned>(entry->prefixLength));
    for (unsigned j = 0; j < entry->prefixLength; ++j)
      Serial.printf(" %02X", static_cast<unsigned>(entry->prefix[j]));
    Serial.println("");
  }
  Serial.println("IMU TRACE END");
}

void runImuControlProbe() {
  stopAllMotors();
  if (!imuAvailable) {
    Serial.println("ERR IMU PROBE requires successful startup; use IMU TRACE for failure details");
    return;
  }
  Serial.println("IMU PROBE BEGIN; raw control reads only, no reset or report writes");
  const bool featureSent = bno08x.probeControl(0xFE, 0x08);
  const uint32_t featureStart = millis();
  while (static_cast<uint32_t>(millis() - featureStart) < 500) {
    pollImu();
    delay(1);
  }
  const bool productSent = bno08x.probeControl(0xF9, 0x00);
  const uint32_t productStart = millis();
  while (static_cast<uint32_t>(millis() - productStart) < 500) {
    pollImu();
    delay(1);
  }
  Serial.printf("IMU PROBE SENT FEATURE %u PRODUCT %u; sent does not mean acknowledged\n",
                featureSent ? 1U : 0U, productSent ? 1U : 0U);
  sendImuDiagnostics();
  sendImuTrace();
}

void processCommand(char *line) {
  while (*line == ' ' || *line == '\t') {
    ++line;
  }

  if (line[0] == 'X' && line[1] == '\0') {
    stopAllMotors();
    watchdogReported = false;
    Serial.println("OK STOP");
    return;
  }

  if (line[0] == '?' && line[1] == '\0') {
    printHelp();
    return;
  }

  if (strcmp(line, "DIAG") == 0) { sendDiagnostics(); return; }
  if (strcmp(line, "IMU RETRY") == 0) { initializeImu(); return; }
  if (strcmp(line, "IMU PROBE") == 0) { runImuControlProbe(); return; }
  if (strcmp(line, "IMU TRACE") == 0) {
    if (motionRequested) Serial.println("ERR IMU TRACE requires stopped motion; send X first");
    else sendImuTrace();
    return;
  }
  if (strcmp(line, "IMU DIAG") == 0) {
    if (motionRequested) Serial.println("ERR IMU DIAG requires stopped motion; send X first");
    else sendImuDiagnostics();
    return;
  }

  if (strcmp(line, "CFG GET") == 0) { printSettings(); return; }
  if (strcmp(line, "CFG SAVE") == 0) {
    stopAllMotors();
    Serial.println(saveSettings() ? "OK CFG SAVE" : "ERR CFG SAVE");
    return;
  }
  if (strcmp(line, "CFG RESET") == 0) {
    stopAllMotors(); resetSettingsToDefaults();
    Serial.println("OK CFG RESET"); printSettings(); return;
  }
  if (strncmp(line, "CFG SET ", 8) == 0) {
    stopAllMotors();
    char key[32] = {0}; float value = 0.0F; char extra = '\0';
    if (sscanf(line + 8, "%31s %f %c", key, &value, &extra) != 2 || !isfinite(value)) {
      Serial.println("ERR CFG malformed"); return;
    }
    bool valid = true;
    if (strcmp(key, "pwm-fl") == 0 && value >= 0 && value <= 255) settings.pwm[0] = lroundf(value);
    else if (strcmp(key, "pwm-fr") == 0 && value >= 0 && value <= 255) settings.pwm[1] = lroundf(value);
    else if (strcmp(key, "pwm-rl") == 0 && value >= 0 && value <= 255) settings.pwm[2] = lroundf(value);
    else if (strcmp(key, "pwm-rr") == 0 && value >= 0 && value <= 255) settings.pwm[3] = lroundf(value);
    else if (strcmp(key, "heading-kp") == 0 && value >= 0 && value <= 5) settings.headingKp = value;
    else if (strcmp(key, "heading-max") == 0 && value >= 0 && value <= 1) settings.headingMax = value;
    else if (strcmp(key, "heading-deadband-deg") == 0 && value >= 0 && value <= 30) settings.headingDeadbandRad = value * NavigationMath::PI_F / 180.0F;
    else if (strcmp(key, "heading-sign") == 0 && (value == -1 || value == 1)) settings.headingSign = static_cast<int8_t>(value);
    else if (strcmp(key, "heading-enabled") == 0 && (value == 0 || value == 1)) settings.headingEnabled = value == 1;
    else valid = false;
    if (!valid) { Serial.println("ERR CFG invalid key or value"); return; }
    applySettingsToMotors();
    Serial.printf("OK CFG SET %s\n", key); return;
  }

  if (line[0] == 'Z' && line[1] == '\0') {
    float currentYaw = 0.0F;
    if (!readCurrentYaw(currentYaw, IMU_MIN_FIELD_ORIENTED_STATUS)) {
      stopAllMotors();
      Serial.println("ERR field zero requires a fresh IMU heading; motors stopped");
      return;
    }
    stopAllMotors();
    fieldReferenceYaw = currentYaw;
    fieldReferenceValid = true;
    Serial.printf("OK FIELD ZERO %.6f\n", fieldReferenceYaw);
    return;
  }

  if (line[0] == 'F' && (line[1] == ' ' || line[1] == '\t')) {
    int enabled = -1;
    char extra = '\0';
    const int fields = sscanf(line, "F %d %c", &enabled, &extra);
    if (fields == 1 && enabled == 0) {
      stopAllMotors();
      fieldOrientedEnabled = false;
      fieldReferenceValid = false;
      Serial.println("OK FIELD 0");
      return;
    }
    if (fields == 1 && enabled == 1) {
      float currentYaw = 0.0F;
      if (!readCurrentYaw(currentYaw, IMU_MIN_FIELD_ORIENTED_STATUS)) {
        stopAllMotors();
        Serial.println("ERR field mode requires a fresh IMU heading; motors stopped");
        return;
      }
      stopAllMotors();
      fieldReferenceYaw = currentYaw;
      fieldReferenceValid = true;
      fieldOrientedEnabled = true;
      Serial.printf("OK FIELD 1 ZERO %.6f\n", fieldReferenceYaw);
      return;
    }

    stopAllMotors();
    Serial.println("ERR malformed field command; motors stopped");
    return;
  }

  float forward = 0.0F;
  float left = 0.0F;
  float ccw = 0.0F;
  char extra = '\0';
  const int fields = sscanf(line, "V %f %f %f %c",
                            &forward, &left, &ccw, &extra);
  if (fields == 3 && isfinite(forward) && isfinite(left) && isfinite(ccw)) {
    applyVelocity(forward, left, ccw);
    return;
  }

  // Unknown or malformed motion input fails safe.
  stopAllMotors();
  Serial.println("ERR malformed command; motors stopped");
}

void receiveSerialCommands() {
  for (uint8_t budget = 0; budget < 64 && Serial.available() > 0; ++budget) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\n' || incoming == '\r') {
      if (rxDiscardLine) { rxDiscardLine = false; rxLength = 0; continue; }
      if (rxLength > 0) {
        rxBuffer[rxLength] = '\0';
        processCommand(rxBuffer);
        rxLength = 0;
      }
      continue;
    }

    if (rxDiscardLine) continue;
    if (incoming == '\0') { rxDiscardLine = true; rxLength = 0; stopAllMotors(); continue; }
    if (rxLength < RX_BUFFER_SIZE - 1) {
      rxBuffer[rxLength++] = incoming;
    } else {
      rxLength = 0;
      stopAllMotors();
      rxDiscardLine = true;
      Serial.println("ERR command too long; motors stopped");
    }
  }
}

void setup() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    pinMode(motors[i].dir1Pin, OUTPUT);
    pinMode(motors[i].dir2Pin, OUTPUT);
    digitalWrite(motors[i].dir1Pin, LOW);
    digitalWrite(motors[i].dir2Pin, LOW);
  }

  // UART transmission runs asynchronously so telemetry/help bursts do not
  // wait on every outgoing byte while SPI interrupt service is pending.
  const size_t txBufferSize = Serial.setTxBufferSize(SERIAL_TX_BUFFER_SIZE);
  Serial.begin(SERIAL_BAUD);
  if (txBufferSize != SERIAL_TX_BUFFER_SIZE) {
    Serial.println("WARN serial TX buffer unavailable; IMU polling may be delayed");
  }
  delay(500);

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (!attachPwm(i)) {
      stopAllMotors();
      Serial.printf("FATAL PWM attach failed for %s\n", motors[i].name);
      while (true) {
        delay(1000);
      }
    }
    zeroMotorOutputs();

    pinMode(motors[i].encoderAPin, motors[i].encoderAPin >= 34 ? INPUT : INPUT_PULLUP);
    pinMode(motors[i].encoderBPin, motors[i].encoderBPin >= 34 ? INPUT : INPUT_PULLUP);
    previousEncoderState[i] = sampleEncoderState(motors[i].encoderAPin, motors[i].encoderBPin);
  }

  attachInterrupt(digitalPinToInterrupt(motors[FRONT_LEFT].encoderAPin),
                  frontLeftEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[FRONT_LEFT].encoderBPin),
                  frontLeftEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[FRONT_RIGHT].encoderAPin),
                  frontRightEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[FRONT_RIGHT].encoderBPin),
                  frontRightEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[REAR_LEFT].encoderAPin),
                  rearLeftEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[REAR_LEFT].encoderBPin),
                  rearLeftEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[REAR_RIGHT].encoderAPin),
                  rearRightEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(motors[REAR_RIGHT].encoderBPin),
                  rearRightEncoderISR, CHANGE);

  stopAllMotors();
  motorMutex = xSemaphoreCreateMutex();
  if (!motorMutex || xTaskCreate(motorOutputTask, "motor-safety", 3072, nullptr, 3, nullptr) != pdPASS) {
    stopAllMotors();
    Serial.println("FATAL motor safety task unavailable; motion disabled");
    while (true) delay(1000);
  }
  loadSettings();
  initializeImu();

  lastCommandMs = millis();
  lastTelemetryMs = millis();
  Serial.println("READY ESP32_MAKER_MECANUM_IMU_V1");
  printHelp();
}

void loop() {
  xSemaphoreTake(motorMutex, portMAX_DELAY);
  const bool expired = motorWatchdogTripped;
  motorWatchdogTripped = false;
  xSemaphoreGive(motorMutex);
  if (expired) {
    stopAllMotors();
    Serial.println("FAULT WATCHDOG; motors stopped by independent output task");
  }
  receiveSerialCommands();
  pollImu();

  const uint32_t nowMs = millis();
  if (commandActive && nowMs - lastCommandMs > COMMAND_TIMEOUT_MS) {
    stopAllMotors();
    if (!watchdogReported) {
      Serial.println("FAULT WATCHDOG; motors stopped");
      watchdogReported = true;
    }
  }

  if (nowMs - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    sendTelemetry();
    lastTelemetryMs = nowMs;
  }

  delay(1);
}
