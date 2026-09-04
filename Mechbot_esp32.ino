/*
  ESP32-S3 mecanum USB controller - temporary open-loop version

  Serial protocol at 115200 baud:
    V <forward> <left> <ccw>   Values are normalized from -1.0 to +1.0
    X                         Immediate stop
    ?                         Print help

  Examples:
    V 1 0 0       forward
    V 0 1 0       strafe left
    V 0 0 -1      rotate right

  Safety:
    - Motors start disabled.
    - A valid V command must arrive at least every 300 ms.
    - A timeout or malformed motion command stops all motors.

  Current front motors do not have encoders. Front encoder counts will remain 0.
  Replace the open-loop PWM scaling with per-wheel PID after matching encoder
  motors are installed on all four wheels.
*/

#include <Arduino.h>
#include <esp_arduino_version.h>

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint32_t COMMAND_TIMEOUT_MS = 300;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 200;
constexpr float ENCODER_COUNTS_PER_REV = 2500.0F;
constexpr size_t RX_BUFFER_SIZE = 80;

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
  {"FL", 12, 13, 38, 10, 11, 230, +1, +1},
  {"FR", 14, 15, 39,  6,  7, 230, +1, +1},
  {"RL", 16, 17, 40,  8,  9, 177, +1, +1},
  {"RR", 18, 21, 41,  4,  5, 172, +1, +1}
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

char rxBuffer[RX_BUFFER_SIZE];
size_t rxLength = 0;
uint32_t lastCommandMs = 0;
uint32_t lastTelemetryMs = 0;
bool commandActive = false;
bool watchdogReported = false;

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

void applyVelocity(float forward, float left, float ccw) {
  forward = constrain(forward, -1.0F, 1.0F);
  left = constrain(left, -1.0F, 1.0F);
  ccw = constrain(ccw, -1.0F, 1.0F);

  // Mecanum inverse kinematics matching the verified WASD/QE directions.
  float wheel[MOTOR_COUNT] = {
    forward - left - ccw,  // FL
    forward + left + ccw,  // FR
    forward + left - ccw,  // RL
    forward - left + ccw   // RR
  };

  float largest = 1.0F;
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    largest = max(largest, fabsf(wheel[i]));
  }
  for (uint8_t i = 0; i < MOTOR_COUNT; ++i) {
    setMotorCommand(i, wheel[i] / largest);
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

void sendTelemetry() {
  Serial.printf("T %lu %ld %ld %ld %ld\n",
                static_cast<unsigned long>(millis()),
                static_cast<long>(getEncoderCount(FRONT_LEFT)),
                static_cast<long>(getEncoderCount(FRONT_RIGHT)),
                static_cast<long>(getEncoderCount(REAR_LEFT)),
                static_cast<long>(getEncoderCount(REAR_RIGHT)));
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  V <forward> <left> <ccw>   each value -1.0 to +1.0");
  Serial.println("  X                         immediate stop");
  Serial.println("  ?                         help");
  Serial.println("Watchdog: 300 ms. Telemetry: T <ms> <FL> <FR> <RL> <RR>");
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
  lastCommandMs = millis();
  lastTelemetryMs = millis();
  Serial.println("READY ESP32_MECANUM_USB_V1");
  printHelp();
}

void loop() {
  receiveSerialCommands();

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
