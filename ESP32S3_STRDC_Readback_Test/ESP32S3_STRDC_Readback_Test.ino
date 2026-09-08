// Diagnostic harness around the unchanged STRDC BNO08X module and ESP32 HAL.
// This is intentionally separate from the faithful STRDC example sketches.
#include "src/vendor/BNO08X.h"

static bno08x_t bno;
static constexpr uint32_t OBSERVE_MS = 35000;
struct ReportStats {
  uint8_t id;
  uint32_t count;
  uint32_t previous;
  uint32_t lastMs;
};
static ReportStats stats[2];
static uint8_t reportCount;
static uint32_t observationStart;
static uint32_t resetCount;

static void help() {
  Serial.println("STRDC_READBACK V1 | SDA8 SCL9 RST4 P0=5 INT6 | 115200");
  Serial.println("a=accel50 g=gyro50 r=rawgyro50 m=geomagRV20 b=accel+gyro50");
  Serial.println("h=gyro50 at400kHz; other tests use100kHz. ?=help");
  Serial.println("One command = fresh HW init, one Set/Get, 35s observation; then idle.");
  Serial.println("Counts are lower bounds: one newData indication may contain bundled samples.");
}

static void printId(const char *phase) {
  Serial.printf("ID_%s valid=%u PN=%lu build=%lu version=%u.%u.%u reset_cause=%u\n",
    phase, bno.isID, (unsigned long)bno.metaData.swPN,
    (unsigned long)bno.metaData.swBN, bno.metaData.swMaj,
    bno.metaData.swMin, bno.metaData.swPatch, bno.metaData.rstCause);
}

static void getConfig(uint8_t id, const char *phase) {
  uint8_t ignoredFlags = 0;
  uint16_t sensitivity = 0;
  uint32_t interval = 0, batch = 0, configWord = 0;
  const uint32_t started = millis();
  const uint8_t result = bno08x_feature_get(&bno, id, &ignoredFlags,
    &sensitivity, &interval, &batch, &configWord);
  Serial.printf("GET_%s id=0x%02X result=%u elapsed_ms=%lu", phase, id,
    result, (unsigned long)(millis() - started));
  if (result == 0) {
    Serial.printf(" interval_us=%lu batch_us=%lu sensitivity=%u config=0x%08lX",
      (unsigned long)interval, (unsigned long)batch, sensitivity,
      (unsigned long)configWord);
  }
  Serial.println(); // Known incorrect upstream flags extraction is not used.
}

static void probeId() {
  // Same six-byte request as STRDC's private bno08x_get_ID; one I2C write.
  uint8_t request[] = {6, 0, BNO08X_CHANNEL_SENSOR_CTRL, bno.ch2Seq,
    BNO08X_REPORT_PRODUCT_ID_REQUEST, 0};
  bno.isID = false;
  i2c_set_addr(&i2c1, bno.busAddr);
  const uint8_t writeResult = i2c_write(&i2c1, request, sizeof(request));
  const uint32_t started = millis();
  if (writeResult == 0) {
    ++bno.ch2Seq;
    while (!bno.isID && millis() - started < 250) {
      bno08x_get_messages(&bno);
      delay(1);
    }
  }
  Serial.printf("ID_PROBE write_result=%u response=%u elapsed_ms=%lu\n",
    writeResult, bno.isID, (unsigned long)(millis() - started));
  if (bno.isID) printId("FINAL");
}

static void countFresh() {
  for (uint8_t i = 0; i < reportCount; ++i) {
    if (bno.reports[stats[i].id].newData) {
      ++stats[i].count;
      stats[i].lastMs = millis();
      bno.reports[stats[i].id].newData = false;
    }
  }
  if (bno.isRst) {
    ++resetCount;
    bno.isRst = false;
    Serial.printf("POST_INIT_RESET elapsed_ms=%lu total=%lu\n",
      (unsigned long)(millis() - observationStart), (unsigned long)resetCount);
  }
}

static void printStats(const char *label) {
  const uint32_t now = millis();
  Serial.printf("%s elapsed_ms=%lu resets=%lu INT=%d", label,
    (unsigned long)(now - observationStart), (unsigned long)resetCount,
    digitalRead(6));
  for (uint8_t i = 0; i < reportCount; ++i) {
    const long age = stats[i].count ? (long)(now - stats[i].lastMs) : -1;
    Serial.printf(" | id=0x%02X count=%lu new=%lu age_ms=%ld", stats[i].id,
      (unsigned long)stats[i].count,
      (unsigned long)(stats[i].count - stats[i].previous), age);
    stats[i].previous = stats[i].count;
  }
  Serial.println();
}

static void runTrial(char command) {
  memset(&bno, 0, sizeof(bno));
  memset(stats, 0, sizeof(stats));
  reportCount = command == 'b' ? 2 : 1;
  stats[0].id = command == 'a' || command == 'b' ? BNO08X_SENSOR_ACCEL :
    command == 'r' ? BNO08X_SENSOR_GYRO_RAW :
    command == 'm' ? BNO08X_SENSOR_GEOMAG_ROT_VECTOR : BNO08X_SENSOR_GYRO;
  stats[1].id = BNO08X_SENSOR_GYRO;
  const uint32_t busHz = command == 'h' ? 400000 : 100000;
  const uint32_t interval = command == 'm' ? 50000 : 20000;
  Serial.printf("TRIAL command=%c bus_hz=%lu interval_us=%lu\n", command,
    (unsigned long)busHz, (unsigned long)interval);

  Wire.end(); // Reopen so the requested bus speed applies to every trial.
  i2c_open(&i2c1, busHz);
  bno.bus = &i2c1;
  bno.busType = BNO08X_I2C;
  bno.busAddr = 0x4A;
  bno.pinRst = 4;
  bno.wakePin = 5;
  bno.pinInt = 6;
  const uint32_t initStarted = millis();
  const uint8_t initResult = bno08x_init(&bno); // Includes the vendor HW reset.
  Serial.printf("INIT result=%u elapsed_ms=%lu\n", initResult,
    (unsigned long)(millis() - initStarted));
  if (initResult != 0) {
    Serial.println("DONE init_failed; idle, no retry.");
    return;
  }
  printId("INIT");
  for (uint8_t i = 0; i < reportCount; ++i) {
    const uint32_t started = millis();
    const uint8_t result = bno08x_feature_set(&bno, stats[i].id, 0, 0,
      interval, 0, 0);
    Serial.printf("SET id=0x%02X result=%u elapsed_ms=%lu\n", stats[i].id,
      result, (unsigned long)(millis() - started));
    getConfig(stats[i].id, "INITIAL"); // Always ask, even after Set timeout.
  }

  for (uint8_t i = 0; i < reportCount; ++i)
    bno.reports[stats[i].id].newData = false; // Count only observation phase.
  bno.isRst = false;
  resetCount = 0;
  observationStart = millis();
  uint32_t lastPrint = observationStart;
  Serial.println("OBSERVE_BEGIN duration_ms=35000; no calibration command was sent.");
  while (millis() - observationStart < OBSERVE_MS) {
    bno08x_get_messages(&bno); // Vendor polling; no ISR attached by this harness.
    countFresh();
    if (millis() - lastPrint >= 1000) {
      printStats("LIVE");
      lastPrint = millis();
    }
    delay(1);
  }
  printStats("SUMMARY");
  for (uint8_t i = 0; i < reportCount; ++i) getConfig(stats[i].id, "FINAL");
  probeId();
  Serial.println("DONE idle; observation counters stopped. Send one new command to reset/test.");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  help(); // No bus initialization or IMU reset until a literal command arrives.
}

void loop() {
  if (!Serial.available()) { delay(5); return; }
  const char command = Serial.read();
  if (command == '\r' || command == '\n' || command == ' ') return;
  if (command == '?') { help(); return; }
  if (command != 'a' && command != 'g' && command != 'r' && command != 'm' &&
      command != 'b' && command != 'h') {
    Serial.println("Unknown command; send a, g, r, m, b, h or ?.");
    return;
  }
  runTrial(command);
  // Avoid executing a sequence accidentally queued during a running trial.
  while (Serial.available()) Serial.read();
}
