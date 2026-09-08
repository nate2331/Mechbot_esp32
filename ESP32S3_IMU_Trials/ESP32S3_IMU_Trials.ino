/*
  CUSTOM DIAGNOSTIC, not the STRDC example. See README.md for provenance.
  S3 3V3->VDC, GND->GND, 8->SDA, 9->SCL, 4->RST, 5->P0, 6->INT.
  Each serial command performs one finite trial from a hardware reset.
  Every decoded sensor callback is counted, including bundled reports.
  No motors, firmware update, flash/FRS write, or calibration command.
*/
#include <Arduino.h>
#include <Wire.h>
#include "src/adafruit_bno08x/Adafruit_BNO08x.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module."
#endif

constexpr int PIN_SDA = 8, PIN_SCL = 9, PIN_RST = 4, PIN_P0 = 5, PIN_INT = 6;
constexpr uint32_t TRIAL_MS = 35000;
constexpr unsigned SLOT_COUNT = SH2_MAX_SENSOR_ID + 1;

struct ReportStats {
  uint32_t count, previous, firstMs, lastMs, sequenceGaps, duplicateSequences;
  uint8_t sequence, status;
  float value[4];
};
ReportStats reports[SLOT_COUNT] = {};
sh2_SensorId_t requested[2] = {};
uint32_t requestedUs[2] = {};
unsigned requestCount = 0;
uint32_t trialNumber = 0, startMs = 0, lastStatusMs = 0, frozenMs = 0;
uint32_t bootResets = 0, unexpectedResets = 0, decodeErrors = 0, transportEvents = 0;
bool starting = false, active = false, counting = false, resetStop = false;
bool haveResult = false;
char trialCommand = '-';
const char *trialName = "IDLE";

void onSensor(void *, sh2_SensorEvent_t *raw);
void onAsync(void *, sh2_AsyncEvent_t *event);

class DiagnosticBNO : public Adafruit_BNO08x {
 public:
  DiagnosticBNO() : Adafruit_BNO08x(-1) {}
  bool sessionOpen = false;
  int openResult = SH2_ERR, productResult = SH2_ERR;
  void closeSession() {
    if (sessionOpen) sh2_close();
    sessionOpen = false;
  }
 protected:
  // Retain the Adafruit I2C HAL. Override initialization only so we receive
  // EVERY reset and sensor event. Hardware reset is explicit in startTrial;
  // upstream HAL open still sends its standard software-reset packet.
  bool _init(int32_t) override {
    openResult = sh2_open(&_HAL, onAsync, nullptr);
    sessionOpen = openResult == SH2_OK;
    if (!sessionOpen) return false;
    sh2_setSensorCallback(onSensor, nullptr);
    memset(&prodIds, 0, sizeof(prodIds));
    productResult = sh2_getProdIds(&prodIds);
    return productResult == SH2_OK;
  }
};
DiagnosticBNO imu;

void onAsync(void *, sh2_AsyncEvent_t *event) {
  if (event->eventId == SH2_RESET) {
    if (starting) ++bootResets;
    else if (active) {
      ++unexpectedResets;
      resetStop = true;
    }
  } else if (event->eventId == SH2_SHTP_EVENT) {
    ++transportEvents;
  }
}

void onSensor(void *, sh2_SensorEvent_t *raw) {
  if (!counting || resetStop) return;
  sh2_SensorValue_t v = {};
  if (sh2_decodeSensorEvent(&v, raw) != SH2_OK) {
    ++decodeErrors;
    return;
  }
  if (v.sensorId >= SLOT_COUNT) { ++decodeErrors; return; }
  ReportStats &s = reports[v.sensorId];
  const uint32_t now = millis();
  if (s.count) {
    const uint8_t step = static_cast<uint8_t>(v.sequence - s.sequence);
    if (step == 0) ++s.duplicateSequences;
    else s.sequenceGaps += step - 1;
  } else s.firstMs = now;
  ++s.count;
  s.sequence = v.sequence;
  s.status = v.status;
  s.lastMs = now;
  switch (v.sensorId) {
    case SH2_ACCELEROMETER:
      s.value[0] = v.un.accelerometer.x; s.value[1] = v.un.accelerometer.y;
      s.value[2] = v.un.accelerometer.z; break;
    case SH2_GYROSCOPE_CALIBRATED:
      s.value[0] = v.un.gyroscope.x; s.value[1] = v.un.gyroscope.y;
      s.value[2] = v.un.gyroscope.z; break;
    case SH2_RAW_GYROSCOPE:
      s.value[0] = v.un.rawGyroscope.x; s.value[1] = v.un.rawGyroscope.y;
      s.value[2] = v.un.rawGyroscope.z; s.value[3] = v.un.rawGyroscope.temperature; break;
    case SH2_GEOMAGNETIC_ROTATION_VECTOR:
      s.value[0] = v.un.geoMagRotationVector.real; s.value[1] = v.un.geoMagRotationVector.i;
      s.value[2] = v.un.geoMagRotationVector.j; s.value[3] = v.un.geoMagRotationVector.k; break;
  }
}

void printHelp() {
  Serial.println("IMU_TRIALS V1 CUSTOM | UART 115200 | SDA8 SCL9 RST4 P0=5 INT6");
  Serial.println("a=accel50 g=gyro50 r=rawgyro50 m=geomagRV20 b=accel50+gyro50");
  Serial.println("l=gyro10 h=gyro50/I2C400k; other trials I2C100k; each 35 sec");
  Serial.println("s=last snapshot x=stop ?=help; idle until a trial command");
}

void printProducts(const char *phase, const sh2_ProductIds_t &ids, int rc) {
  Serial.printf("%s ID rc=%d entries=%u\n", phase, rc, ids.numEntries);
  if (rc != SH2_OK) return;
  for (unsigned i = 0; i < ids.numEntries && i < SH2_MAX_PROD_ID_ENTRIES; ++i) {
    const sh2_ProductId_t &p = ids.entry[i];
    Serial.printf(" ID%u part=%lu ver=%u.%u.%u build=%lu cause=%u\n", i,
      (unsigned long)p.swPartNumber, p.swVersionMajor, p.swVersionMinor,
      p.swVersionPatch, (unsigned long)p.swBuildNumber, p.resetCause);
  }
}

void readConfigs(const char *phase) {
  for (unsigned i = 0; i < requestCount; ++i) {
    sh2_SensorConfig_t actual = {};
    const uint32_t before = millis();
    const int rc = sh2_getSensorConfig(requested[i], &actual);
    Serial.printf("%s CFG id=0x%02X rc=%d want_us=%lu got_us=%lu time_ms=%lu\n",
      phase, requested[i], rc, (unsigned long)requestedUs[i],
      (unsigned long)actual.reportInterval_us, (unsigned long)(millis() - before));
  }
}

void readErrors() {
  sh2_ErrorRecord_t errors[8];
  memset(errors, 0xFF, sizeof(errors));
  uint16_t count = 8;
  const uint32_t before = millis();
  const int rc = sh2_getErrors(0xFF, errors, &count);
  unsigned captured = 0;
  while (captured < 8 && errors[captured].source != 0xFF) ++captured;
  Serial.printf("POST ERR rc=%d captured=%u complete=%d time_ms=%lu\n", rc,
    captured, rc == SH2_OK, (unsigned long)(millis() - before));
  for (unsigned i = 0; i < captured; ++i) {
    const sh2_ErrorRecord_t &e = errors[i];
    Serial.printf(" ERR%u sev=%u seq=%u src=%u err=%u module=%u code=%u hex=%02X %02X %02X %02X %02X %02X\n",
      i, e.severity, e.sequence, e.source, e.error, e.module, e.code,
      e.severity, e.sequence, e.source, e.error, e.module, e.code);
  }
}

void printStatus(const char *tag, uint32_t now, bool finalDetails) {
  Serial.printf("%s #%lu %c %s t=%lums reset=%lu decode=%lu transport=%lu INT=%d\n",
    tag, (unsigned long)trialNumber, trialCommand, trialName,
    (unsigned long)(now - startMs), (unsigned long)unexpectedResets,
    (unsigned long)decodeErrors, (unsigned long)transportEvents, digitalRead(PIN_INT));
  for (unsigned i = 0; i < SLOT_COUNT; ++i) {
    bool wanted = false;
    for (unsigned j = 0; j < requestCount; ++j) wanted |= requested[j] == i;
    ReportStats &s = reports[i];
    if (!wanted && !s.count) continue;
    const long age = s.count ? (long)(now - s.lastMs) : -1;
    Serial.printf(" 0x%02X %s n=%lu new=%lu age=%ld gap=%lu dup=%lu", i,
      resetStop ? "RESET" : !s.count ? "WAIT" : age > 1000 ? "STALE" : "LIVE",
      (unsigned long)s.count, (unsigned long)(s.count - s.previous), age,
      (unsigned long)s.sequenceGaps, (unsigned long)s.duplicateSequences);
    if (finalDetails && s.count) {
      Serial.printf(" first=%lu last=%lu status=%u v=%.4f,%.4f,%.4f,%.4f",
        (unsigned long)(s.firstMs - startMs), (unsigned long)(s.lastMs - startMs),
        s.status, s.value[0], s.value[1], s.value[2], s.value[3]);
    }
    Serial.println();
    if (!finalDetails) s.previous = s.count;
  }
}

void finishTrial(const char *reason) {
  if (!active) return;
  frozenMs = millis();
  counting = false;
  active = false;
  haveResult = true;
  Serial.printf("RESULT reason=%s boot_resets=%lu\n", reason, (unsigned long)bootResets);
  printStatus("FINAL", frozenMs, true);
  // Observe the sensor/control state before disabling reports or resetting it.
  if (imu.sessionOpen) {
    readConfigs("POST");
    sh2_ProductIds_t ids = {};
    const int rc = sh2_getProdIds(&ids);
    printProducts("POST", ids, rc);
    readErrors();
    for (unsigned i = 0; i < requestCount; ++i) {
      sh2_SensorConfig_t off = {};
      const int offResult = sh2_setSensorConfig(requested[i], &off);
      Serial.printf("OFF id=0x%02X rc=%d\n", requested[i], offResult);
    }
    imu.closeSession();
  }
  active = false;
  Serial.printf("END #%lu; s=reprint result, next command starts fresh reset\n", (unsigned long)trialNumber);
}

void startTrial(char command) {
  if (active) { Serial.println("BUSY: trial in progress; x=stop"); return; }
  uint32_t busHz = 100000;
  requestCount = 1;
  requestedUs[0] = 20000;
  switch (command) {
    case 'a': trialName = "ACCEL50"; requested[0] = SH2_ACCELEROMETER; break;
    case 'g': trialName = "GYRO50"; requested[0] = SH2_GYROSCOPE_CALIBRATED; break;
    case 'r': trialName = "RAW_GYRO50"; requested[0] = SH2_RAW_GYROSCOPE; break;
    case 'm': trialName = "GEOMAG_RV20"; requested[0] = SH2_GEOMAGNETIC_ROTATION_VECTOR; requestedUs[0] = 50000; break;
    case 'b': trialName = "ACCEL50_GYRO50"; requested[0] = SH2_ACCELEROMETER;
      requested[1] = SH2_GYROSCOPE_CALIBRATED; requestedUs[1] = 20000; requestCount = 2; break;
    case 'l': trialName = "GYRO10"; requested[0] = SH2_GYROSCOPE_CALIBRATED; requestedUs[0] = 100000; break;
    case 'h': trialName = "GYRO50_400K"; requested[0] = SH2_GYROSCOPE_CALIBRATED; busHz = 400000; break;
    default: return;
  }
  imu.closeSession();
  memset(reports, 0, sizeof(reports));
  bootResets = unexpectedResets = decodeErrors = transportEvents = 0;
  resetStop = counting = haveResult = false;
  trialCommand = command;
  ++trialNumber;
  Serial.printf("BEGIN #%lu %c %s I2C=%lu\n", (unsigned long)trialNumber,
    command, trialName, (unsigned long)busHz);
  // P0 must remain low across reset for I2C. This is NOT a full power cycle.
  digitalWrite(PIN_P0, LOW);
  digitalWrite(PIN_RST, LOW);
  delay(20);
  Wire.end();
  const bool busOk = Wire.begin(PIN_SDA, PIN_SCL, busHz);
  Wire.setTimeOut(50);
  digitalWrite(PIN_RST, HIGH);
  delay(400);
  Serial.printf("RESET commanded low20ms released400ms RST=%d P0=%d bus=%d\n",
    digitalRead(PIN_RST), digitalRead(PIN_P0), busOk);
  if (!busOk) { Serial.println("END FAIL_BUS_START"); return; }
  uint8_t address = 0;
  for (uint8_t addr = 0x4A; addr <= 0x4B; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t rc = Wire.endTransmission();
    Serial.printf("ACK 0x%02X rc=%u\n", addr, rc);
    if (!rc && !address) address = addr;
  }
  if (!address) { Serial.println("END FAIL_NO_ACK"); return; }
  starting = true;
  imu.openResult = imu.productResult = SH2_ERR;
  const bool initOk = imu.begin_I2C(address, &Wire);
  starting = false;
  Wire.setClock(busHz);
  Serial.printf("BOOT init=%d open_rc=%d id_rc=%d reset_events=%lu clock=%lu\n",
    initOk, imu.openResult, imu.productResult, (unsigned long)bootResets,
    (unsigned long)Wire.getClock());
  printProducts("PRE", imu.prodIds, imu.productResult);
  if (!initOk || !bootResets) {
    imu.closeSession();
    Serial.println("END FAIL_INIT_OR_NO_RESET_EVENT");
    return;
  }
  // Count from the first Set Feature write, including reports received while
  // synchronous configuration readback services bundled sensor packets.
  startMs = lastStatusMs = millis();
  active = counting = true;
  for (unsigned i = 0; i < requestCount; ++i) {
    sh2_SensorConfig_t config = {};
    config.reportInterval_us = requestedUs[i];
    const int rc = sh2_setSensorConfig(requested[i], &config);
    Serial.printf("SET id=0x%02X interval_us=%lu rc=%d\n", requested[i],
      (unsigned long)requestedUs[i], rc);
  }
  readConfigs("PRE");
  if (resetStop) finishTrial("UNEXPECTED_RESET");
}

void setup() {
  Serial.begin(115200);
  const uint32_t before = millis();
  while (!Serial && millis() - before < 3000) delay(10);
  pinMode(PIN_P0, OUTPUT); digitalWrite(PIN_P0, LOW);
  pinMode(PIN_RST, OUTPUT); digitalWrite(PIN_RST, HIGH);
  pinMode(PIN_INT, INPUT_PULLUP);
  delay(500);
  printHelp();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c == '?') printHelp();
    else if (c == 'x') finishTrial("USER_STOP");
    else if (c == 's') {
      if (active) printStatus("SNAP", millis(), true);
      else if (haveResult) printStatus("SAVED", frozenMs, true);
      else Serial.println("No completed result yet.");
    } else if (c == 'a' || c == 'g' || c == 'r' || c == 'm' ||
               c == 'b' || c == 'l' || c == 'h') startTrial(c);
  }
  if (active) {
    sh2_service();
    const uint32_t now = millis();
    if (resetStop) finishTrial("UNEXPECTED_RESET");
    else if (now - startMs >= TRIAL_MS) finishTrial("35_SECONDS");
    else if (now - lastStatusMs >= 1000) {
      printStatus("RUN", now, false);
      lastStatusMs = now;
    }
  }
  delay(1);
}
