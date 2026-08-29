/*
  ESP32-S3 mecanum USB controller with BNO085 IMU

  Serial protocol at 115200 baud:
    V <forward> <left> <ccw>   Values are normalized from -1.0 to +1.0
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
    - Motors start disabled.
    - A valid V command must arrive at least every 300 ms.
    - A timeout or malformed motion command stops all motors.
    - An IMU failure is reported but does not disable motor control.
    - Heading hold is enabled by default and yields to deliberate turn input.
    - Field-oriented control defaults to disabled.
    - Field-oriented motion stops if its required heading becomes unavailable.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <esp_arduino_version.h>

#include "NavigationMath.h"

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint32_t COMMAND_TIMEOUT_MS = 300;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 200;
constexpr float ENCODER_COUNTS_PER_REV = 2500.0F;
constexpr size_t RX_BUFFER_SIZE = 80;

constexpr int IMU_SDA_PIN = 1;
constexpr int IMU_SCL_PIN = 2;
constexpr uint8_t IMU_I2C_ADDRESS = 0x4A;
constexpr uint32_t IMU_I2C_FREQUENCY_HZ = 100000;
constexpr uint32_t IMU_REPORT_INTERVAL_US = 20000;  // 50 Hz
constexpr uint32_t IMU_STALE_MS = 500;
constexpr uint32_t IMU_RETRY_INTERVAL_MS = 2000;
constexpr uint32_t IMU_FULL_REINIT_MS = 2000;
// Relative heading hold only needs a fresh quaternion; SH-2 can report useful
// short-term yaw while its absolute accuracy status is still 0. Field-oriented
// control retains the stricter calibrated-heading requirement.
constexpr uint8_t IMU_MIN_HEADING_HOLD_STATUS = 0;
constexpr uint8_t IMU_MIN_FIELD_ORIENTED_STATUS = 1;

// Starting values only. Tune on blocks at low speed before unrestricted use.
constexpr float HEADING_HOLD_KP = 0.70F;              // turn command / radian
constexpr float HEADING_HOLD_MAX_CORRECTION = 0.30F;  // normalized turn command
constexpr float HEADING_HOLD_ERROR_DEADBAND_RAD =
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
  uint8_t pwmPin;
  uint8_t encoderAPin;
  uint8_t encoderBPin;
  uint8_t matchedPwm;
  int8_t motorPolarity;
  int8_t encoderPolarity;
};

MotorConfig motors[MOTOR_COUNT] = {
  {"FL", 12, 13, 38,  4,  5, 230, +1, +1},
  {"FR", 14, 15, 39,  6,  7, 230, +1, +1},
  {"RL", 16, 17, 40,  8,  9, 177, +1, +1},
  {"RR", 18, 21, 41, 10, 11, 143, +1, -1}
};

volatile int32_t encoderCounts[MOTOR_COUNT] = {0, 0, 0, 0};
volatile uint8_t previousEncoderState[MOTOR_COUNT] = {0, 0, 0, 0};
portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;

DRAM_ATTR const int8_t quadratureDelta[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

#if ESP_ARDUINO_VERSION_MAJOR < 3
constexpr uint8_t pwmChannels[MOTOR_COUNT] = {0, 1, 2, 3};
#endif

Adafruit_BNO08x bno08x(-1);
sh2_SensorValue_t imuEvent;

bool imuAvailable = false;
bool imuQuaternionValid = false;
uint8_t imuStatus = 0;
uint32_t lastImuQuaternionMs = 0;
uint32_t lastImuInitAttemptMs = 0;

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

void IRAM_ATTR updateEncoder(uint8_t index) {
  const uint8_t currentState =
      (static_cast<uint8_t>(digitalRead(motors[index].encoderAPin)) << 1) |
       static_cast<uint8_t>(digitalRead(motors[index].encoderBPin));
  const uint8_t transition = (previousEncoderState[index] << 2) | currentState;
  previousEncoderState[index] = currentState;

  portENTER_CRITICAL_ISR(&encoderMux);
  encoderCounts[index] += quadratureDelta[transition] * motors[index].encoderPolarity;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

void IRAM_ATTR frontLeftEncoderISR()  { updateEncoder(FRONT_LEFT); }
void IRAM_ATTR frontRightEncoderISR() { updateEncoder(FRONT_RIGHT); }
void IRAM_ATTR rearLeftEncoderISR()   { updateEncoder(REAR_LEFT); }
void IRAM_ATTR rearRightEncoderISR()  { updateEncoder(REAR_RIGHT); }

void writePwm(uint8_t index, uint8_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(motors[index].pwmPin, duty);
#else
  ledcWrite(pwmChannels[index], duty);
#endif
}

bool attachPwm(uint8_t index) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return ledcAttach(motors[index].pwmPin, PWM_FREQUENCY_HZ,
                    PWM_RESOLUTION_BITS);
#else
  ledcSetup(pwmChannels[index], PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(motors[index].pwmPin, pwmChannels[index]);
  return true;
#endif
}

void stopMotor(uint8_t index) {
  writePwm(index, 0);
  digitalWrite(motors[index].dir1Pin, LOW);
  digitalWrite(motors[index].dir2Pin, LOW);
}

void stopAllMotors() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    stopMotor(i);
  }
  commandActive = false;
  motionRequested = false;
  headingTargetValid = false;
  lastHeadingError = 0.0F;
  lastHeadingCorrection = 0.0F;
}

void setMotorCommand(uint8_t index, float normalizedCommand) {
  normalizedCommand = constrain(normalizedCommand, -1.0F, 1.0F);
  if (fabsf(normalizedCommand) < 0.01F) {
    stopMotor(index);
    return;
  }

  normalizedCommand *= motors[index].motorPolarity;
  if (normalizedCommand > 0.0F) {
    digitalWrite(motors[index].dir1Pin, HIGH);
    digitalWrite(motors[index].dir2Pin, LOW);
  } else {
    digitalWrite(motors[index].dir1Pin, LOW);
    digitalWrite(motors[index].dir2Pin, HIGH);
  }

  const uint8_t duty = static_cast<uint8_t>(
      roundf(fabsf(normalizedCommand) * motors[index].matchedPwm));
  writePwm(index, duty);
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

void applyVelocity(float forward, float left, float ccw) {
  forward = constrain(forward, -1.0F, 1.0F);
  left = constrain(left, -1.0F, 1.0F);
  ccw = constrain(ccw, -1.0F, 1.0F);

  const bool translationRequested =
      fabsf(forward) >= TRANSLATION_DEADBAND ||
      fabsf(left) >= TRANSLATION_DEADBAND;
  const bool rotationRequested = fabsf(ccw) >= TRANSLATION_DEADBAND;
  const bool manualTurnRequested = fabsf(ccw) >= MANUAL_TURN_DEADBAND;
  float currentYaw = 0.0F;
  const bool headingAvailable = readCurrentYaw(currentYaw);
  const bool fieldHeadingAvailable =
      headingAvailable && imuStatus >= IMU_MIN_FIELD_ORIENTED_STATUS;

  if (fieldOrientedEnabled && translationRequested) {
    if (!fieldHeadingAvailable || !fieldReferenceValid) {
      stopAllMotors();
      if (!navigationImuFaultReported) {
        Serial.println("FAULT field-oriented heading unavailable; motors stopped");
        navigationImuFaultReported = true;
      }
      return;
    }

    const float relativeYaw = NavigationMath::wrapRadians(
        currentYaw - fieldReferenceYaw);
    float robotForward = 0.0F;
    float robotLeft = 0.0F;
    NavigationMath::fieldToRobot(forward, left, relativeYaw,
                                 robotForward, robotLeft);
    forward = robotForward;
    left = robotLeft;
  }

  lastHeadingError = 0.0F;
  lastHeadingCorrection = 0.0F;
  if (!translationRequested) {
    // Capture a fresh target when the next translation begins.
    headingTargetValid = false;
  } else if (manualTurnRequested) {
    // Deliberate rotation always wins; follow it so release holds the new yaw.
    headingTargetValid = headingAvailable;
    if (headingAvailable) {
      headingTargetYaw = currentYaw;
    }
  } else if (!headingAvailable) {
    headingTargetValid = false;
    if (!navigationImuFaultReported) {
      Serial.println("WARN heading hold bypassed: heading unavailable");
      navigationImuFaultReported = true;
    }
  } else {
    if (!headingTargetValid) {
      headingTargetYaw = currentYaw;
      headingTargetValid = true;
    }

    lastHeadingError = NavigationMath::wrapRadians(
        headingTargetYaw - currentYaw);
    lastHeadingCorrection = NavigationMath::boundedProportionalCorrection(
        lastHeadingError, HEADING_HOLD_KP,
        HEADING_HOLD_MAX_CORRECTION,
        HEADING_HOLD_ERROR_DEADBAND_RAD);
    if (lastHeadingCorrection != 0.0F) {
      ccw = constrain(ccw + lastHeadingCorrection, -1.0F, 1.0F);
    }
  }

  if (headingAvailable) {
    navigationImuFaultReported = false;
  }

  motionRequested = translationRequested || rotationRequested;

  // Preserve the full translation vector while mixing simultaneous rotation.
  // The wheel order matches the verified WASD/QE directions.
  float wheel[MOTOR_COUNT];
  NavigationMath::mecanumMix(forward, left, ccw, wheel);
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    setMotorCommand(i, wheel[i]);
  }

  lastCommandMs = millis();
  commandActive = true;
  watchdogReported = false;
}

int32_t getEncoderCount(uint8_t index) {
  portENTER_CRITICAL(&encoderMux);
  const int32_t count = encoderCounts[index];
  portEXIT_CRITICAL(&encoderMux);
  return count;
}

void initializeImu();

bool enableImuReports() {
  if (!bno08x.enableReport(SH2_ROTATION_VECTOR,
                            IMU_REPORT_INTERVAL_US)) {
    Serial.println("WARN IMU rotation vector unavailable");
    return false;
  }
  delay(10);
  return true;
}

void reinitializeStaleImu() {
  const uint32_t nowMs = millis();
  const bool quaternionStale =
      !imuQuaternionValid || nowMs - lastImuQuaternionMs > IMU_FULL_REINIT_MS;
  if (!imuAvailable || !quaternionStale || motionRequested ||
      nowMs - lastImuInitAttemptMs < IMU_RETRY_INTERVAL_MS) {
    return;
  }

  Serial.println("WARN IMU rotation vector stale; full reinitialization");
  initializeImu();
}

void initializeImu() {
  lastImuInitAttemptMs = millis();
  Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
  Wire.setClock(IMU_I2C_FREQUENCY_HZ);
  delay(100);

  if (!bno08x.begin_I2C(IMU_I2C_ADDRESS, &Wire)) {
    imuAvailable = false;
    Serial.println("WARN IMU not detected; motor control remains available");
    return;
  }

  imuAvailable = true;
  imuQuaternionValid = false;
  lastImuQuaternionMs = 0;
  if (enableImuReports()) {
    Serial.println("IMU READY BNO085 ROTATION_VECTOR");
  } else {
    Serial.println("WARN IMU detected but rotation vector report failed");
  }
}

void pollImu() {
  if (!imuAvailable) {
    const uint32_t nowMs = millis();
    if (!motionRequested &&
        nowMs - lastImuInitAttemptMs >= IMU_RETRY_INTERVAL_MS) {
      Serial.println("WARN IMU offline; retrying initialization");
      initializeImu();
    }
    return;
  }

  if (bno08x.wasReset()) {
    imuQuaternionValid = false;
    lastImuQuaternionMs = 0;
    lastImuInitAttemptMs = millis();
    invalidateNavigationReferences("IMU reset");
    Serial.println("WARN IMU reset; restarting reports");
    enableImuReports();
  }

  // Bound the work per loop so IMU traffic cannot starve serial commands.
  for (uint8_t eventsRead = 0; eventsRead < 12; ++eventsRead) {
    if (!bno08x.getSensorEvent(&imuEvent)) {
      break;
    }

    switch (imuEvent.sensorId) {
      case SH2_ROTATION_VECTOR:
        imuQx = imuEvent.un.rotationVector.i;
        imuQy = imuEvent.un.rotationVector.j;
        imuQz = imuEvent.un.rotationVector.k;
        imuQw = imuEvent.un.rotationVector.real;
        imuQuaternionValid = true;
        imuStatus = imuEvent.status;
        lastImuQuaternionMs = millis();
        break;

      default:
        break;
    }
  }

  reinitializeStaleImu();
}

void sendEncoderTelemetry() {
  Serial.printf("T %lu %ld %ld %ld %ld\n",
                static_cast<unsigned long>(millis()),
                static_cast<long>(getEncoderCount(FRONT_LEFT)),
                static_cast<long>(getEncoderCount(FRONT_RIGHT)),
                static_cast<long>(getEncoderCount(REAR_LEFT)),
                static_cast<long>(getEncoderCount(REAR_RIGHT)));
}

void sendImuTelemetry() {
  const uint32_t nowMs = millis();

  if (!imuAvailable) {
    Serial.printf("I %lu OFFLINE\n", static_cast<unsigned long>(nowMs));
    return;
  }

  if (!imuQuaternionValid) {
    Serial.printf("I %lu WAIT Q%u\n",
                  static_cast<unsigned long>(nowMs),
                  imuQuaternionValid ? 1U : 0U);
    return;
  }

  if (nowMs - lastImuQuaternionMs > IMU_STALE_MS) {
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
  Serial.printf("N %lu %.6f %.6f %.6f %.4f 1 %u %u\n",
                static_cast<unsigned long>(nowMs),
                headingAvailable ? currentYaw : 0.0F,
                headingTargetValid ? headingTargetYaw : 0.0F,
                lastHeadingError,
                lastHeadingCorrection,
                fieldOrientedEnabled ? 1U : 0U,
                headingAvailable ? 1U : 0U);
}

void sendTelemetry() {
  sendEncoderTelemetry();
  sendImuTelemetry();
  sendNavigationTelemetry();
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  V <forward> <left> <ccw>   each value -1.0 to +1.0");
  Serial.println("    forward and left are simultaneous continuous components");
  Serial.println("  F <0|1>                    field-oriented control off/on");
  Serial.println("  Z                          re-zero field heading");
  Serial.println("  X                         immediate stop");
  Serial.println("  ?                         help");
  Serial.println("Heading hold: ON (automatic; manual turn input takes priority)");
  Serial.println("Watchdog: 300 ms");
  Serial.println("Encoder: T <ms> <FL> <FR> <RL> <RR>");
  Serial.println("IMU: I <ms> <qx> <qy> <qz> <qw> <gx> <gy> <gz> <ax> <ay> <az> <status>");
  Serial.println("Navigation: N <ms> <yaw> <target> <error> <correction> <hold> <field> <ready>");
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
  if (fields == 3) {
    applyVelocity(forward, left, ccw);
    return;
  }

  // Unknown or malformed motion input fails safe.
  stopAllMotors();
  Serial.println("ERR malformed command; motors stopped");
}

void receiveSerialCommands() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\n' || incoming == '\r') {
      if (rxLength > 0) {
        rxBuffer[rxLength] = '\0';
        processCommand(rxBuffer);
        rxLength = 0;
      }
      continue;
    }

    if (rxLength < RX_BUFFER_SIZE - 1) {
      rxBuffer[rxLength++] = incoming;
    } else {
      rxLength = 0;
      stopAllMotors();
      Serial.println("ERR command too long; motors stopped");
    }
  }
}

void setup() {
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    pinMode(motors[i].dir1Pin, OUTPUT);
    pinMode(motors[i].dir2Pin, OUTPUT);
    pinMode(motors[i].pwmPin, OUTPUT);
    digitalWrite(motors[i].dir1Pin, LOW);
    digitalWrite(motors[i].dir2Pin, LOW);
    digitalWrite(motors[i].pwmPin, LOW);
  }

  Serial.begin(SERIAL_BAUD);
  delay(500);

  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    if (!attachPwm(i)) {
      stopAllMotors();
      Serial.printf("FATAL PWM attach failed for %s\n", motors[i].name);
      while (true) {
        delay(1000);
      }
    }
    writePwm(i, 0);

    pinMode(motors[i].encoderAPin, INPUT_PULLUP);
    pinMode(motors[i].encoderBPin, INPUT_PULLUP);
    previousEncoderState[i] =
        (static_cast<uint8_t>(digitalRead(motors[i].encoderAPin)) << 1) |
         static_cast<uint8_t>(digitalRead(motors[i].encoderBPin));
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
  initializeImu();

  lastCommandMs = millis();
  lastTelemetryMs = millis();
  Serial.println("READY ESP32_MECANUM_USB_IMU_NAV_V3");
  printHelp();
}

void loop() {
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
