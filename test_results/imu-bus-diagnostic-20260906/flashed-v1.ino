// Bounded automatic IMU diagnostics. P1 stays physically tied to GND.
// No persistent sensor configuration writes and no motor commands.
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include "RvcParser.h"

constexpr uint8_t MOTOR_PINS[] = {17,12,14,15,4,2,27,13};
constexpr uint8_t RST = 25, P0 = 16, SDA_PIN = 21, SCL_PIN = 22, INT_PIN = 26;
HardwareSerial uartOne(1), uartTwo(2);
HardwareSerial *activeUart = nullptr;
bool wireStarted = false;
uint8_t address = 0, controlSeq = 0, executableSeq = 0;
uint32_t lastSummaryMs = 0;
struct Result {
  const char *name = "";
  uint32_t bytes = 0, packets = 0, inputs = 0, bad = 0, tx = 0, ioErrors = 0;
  uint32_t interval = 0;
  bool featureSeen = false;
  uint8_t foundAddress = 0;
  uint8_t capture[128] = {};
  unsigned captured = 0;
};
Result results[5];
Result *current = nullptr;
RvcParser rvc;

void printPins() {
  Serial.printf("PINS RST%u P0%u SDA%u SCL%u INT%u\n", digitalRead(RST),
                digitalRead(P0), digitalRead(SDA_PIN), digitalRead(SCL_PIN), digitalRead(INT_PIN));
}
void stopBuses() {
  if (activeUart) { activeUart->end(); activeUart = nullptr; }
  if (wireStarted) { Wire.end(); wireStarted = false; }
}
void holdReset(bool rvcMode) {
  pinMode(RST, OUTPUT);
  digitalWrite(RST, LOW);
  stopBuses();
  pinMode(P0, OUTPUT);
  digitalWrite(P0, rvcMode ? HIGH : LOW);
  for (uint8_t p : {uint8_t(32),uint8_t(33),INT_PIN}) pinMode(p, INPUT);
}
void releaseReset(bool usePullup) {
  if (usePullup) pinMode(RST, INPUT); // Breakout's documented 10k pullup.
  else digitalWrite(RST, HIGH);
}
void dumpBytes(const char *label, const uint8_t *data, unsigned length) {
  Serial.printf("%s", label);
  for (unsigned i = 0; i < length; ++i) Serial.printf(" %02X", data[i]);
  Serial.println("");
}
void serviceRvc(uint32_t duration) {
  const uint32_t start = millis();
  do {
    for (unsigned i = 0; activeUart && i < 512 && activeUart->available(); ++i) {
      const int v = activeUart->read();
      if (v < 0) break;
      const auto b = static_cast<uint8_t>(v);
      ++current->bytes;
      if (current->captured < sizeof(current->capture)) current->capture[current->captured++] = b;
      if (rvc.feed(b)) ++current->inputs;
    }
    delay(1);
  } while (static_cast<uint32_t>(millis()-start) < duration);
  current->bad = rvc.badChecksums();
}
void runRvc(unsigned slot, bool useSecondUart, bool pullupReset) {
  current = &results[slot];
  current->name = useSecondUart ? "RVC_UART2_100MS_PULLUP" : "RVC_UART1_10MS_DRIVEN";
  Serial.printf("PHASE %u %s BEGIN\n", slot, current->name);
  holdReset(true);
  rvc = RvcParser();
  pinMode(SCL_PIN, INPUT);
  activeUart = useSecondUart ? &uartTwo : &uartOne;
  activeUart->setRxBufferSize(2048);
  activeUart->begin(115200, SERIAL_8N1, SDA_PIN, -1);
  delay(useSecondUart ? 100 : 10);
  releaseReset(pullupReset);
  serviceRvc(5000);
  printPins();
  Serial.printf("RVC RESULT BYTES%lu FRAMES%lu BAD%lu\n", (unsigned long)current->bytes,
                (unsigned long)current->inputs, (unsigned long)current->bad);
  dumpBytes("RVC RAW", current->capture, current->captured);
  Serial.printf("RVC TEXT ");
  for (unsigned i = 0; i < current->captured; ++i) {
    const uint8_t b = current->capture[i];
    if (b == '\r') Serial.printf("\\r");
    else if (b == '\n') Serial.printf("\\n");
    else Serial.printf("%c", b >= 32 && b < 127 ? b : '.');
  }
  Serial.println("");
}

void inspectPacket(const uint8_t *p, unsigned n) {
  ++current->packets;
  current->bytes += n;
  if (p[2] >= 3 && p[2] <= 5) {
    ++current->inputs;
    if (current->inputs <= 3) dumpBytes("I2C INPUT", p, n < 40 ? n : 40);
  } else if (current->packets <= 6 || p[2] == 2) {
    dumpBytes("I2C RX", p, n < 48 ? n : 48);
  }
  if (p[2] != 2 || (p[1] & 0x80)) return;
  for (unsigned i = 4; i < n;) {
    const uint8_t *q = p + i;
    const unsigned size = q[0] == 0xFC ? 17 : (q[0] == 0xF1 || q[0] == 0xF8) ? 16 : 0;
    if (!size || n-i < size) break;
    if (q[0] == 0xFC) {
      current->featureSeen = true;
      current->interval = (uint32_t)q[5] | ((uint32_t)q[6]<<8) |
                          ((uint32_t)q[7]<<16) | ((uint32_t)q[8]<<24);
      Serial.printf("FEATURE ID%u INTERVAL%lu\n", q[1], (unsigned long)current->interval);
    }
    if (q[0] == 0xF1 && q[2] == 1) {
      Serial.printf("ERROR RESPONSE CMDSEQ%u SEVERITY%u SEQ%u SOURCE%u ERROR%u MODULE%u CODE%u\n",
                    q[3], q[5], q[6], q[7], q[8], q[9], q[10]);
    }
    i += size;
  }
}
bool readPacket() {
  if (!wireStarted || !address) return false;
  // Read the SHTP header, then reread the complete packet including its header.
  uint8_t header[4], data[384];
  size_t got = Wire.requestFrom(address, size_t(4), true);
  if (got != 4) {
    while (Wire.available()) Wire.read();
    if (got) ++current->ioErrors;
    return false;
  }
  for (unsigned i=0;i<4;++i) header[i] = Wire.read();
  const unsigned n = (header[0] | (unsigned(header[1])<<8)) & 0x7FFF;
  if (n == 0 || n == 0x7FFF) return false;
  if (n < 4 || n > sizeof(data)) { ++current->bad; return false; }
  got = Wire.requestFrom(address, size_t(n), true);
  if (got != n) { while (Wire.available()) Wire.read(); ++current->ioErrors; return false; }
  for (unsigned i=0;i<n;++i) data[i] = Wire.read();
  const unsigned actual = (data[0] | (unsigned(data[1])<<8)) & 0x7FFF;
  if (actual < 4 || actual > n || data[2] > 5) { ++current->bad; return false; }
  inspectPacket(data, actual);
  return true;
}
void serviceI2c(uint32_t duration) {
  const uint32_t start = millis();
  uint32_t lastForced = 0;
  do {
    // Poll periodically even if the physical INT lead is missing.
    if (digitalRead(INT_PIN) == LOW || millis()-lastForced >= 20) {
      readPacket();
      lastForced = millis();
    }
    delay(1);
  } while (static_cast<uint32_t>(millis()-start) < duration);
}
bool sendControl(const uint8_t *payload, unsigned n) {
  if (!address || n > 32) return false;
  uint8_t p[36] = {uint8_t(n+4),0,2,controlSeq};
  memcpy(p+4,payload,n);
  Wire.beginTransmission(address);
  Wire.write(p,n+4);
  const int result = Wire.endTransmission();
  Serial.printf("I2C TX STATUS%d", result);
  for (unsigned i=0;i<n+4;++i) Serial.printf(" %02X",p[i]);
  Serial.println("");
  if (result) { ++current->ioErrors; return false; }
  ++controlSeq; ++current->tx;
  return true;
}
void runI2c(unsigned slot, uint32_t clockHz, uint8_t sensor) {
  current = &results[slot];
  current->name = clockHz == 10000 ? "I2C_10K_ACCEL" : sensor == 1 ? "I2C_100K_ACCEL" : "I2C_100K_GYRO";
  Serial.printf("PHASE %u %s BEGIN\n", slot, current->name);
  holdReset(false);
  pinMode(32, OUTPUT); digitalWrite(32, LOW); // DI=0 selects address 0x4A.
  Wire.setBufferSize(512);
  wireStarted = Wire.begin(SDA_PIN,SCL_PIN,clockHz);
  Wire.setTimeOut(25);
  delay(100);
  releaseReset(true);
  delay(700);
  address = 0; controlSeq = 0; executableSeq = 0;
  if (wireStarted) for (uint8_t a : {uint8_t(0x4A),uint8_t(0x4B)}) {
    Wire.beginTransmission(a);
    const int status = Wire.endTransmission();
    Serial.printf("I2C SCAN ADDR%02X STATUS%d\n",a,status);
    if (!status && !address) address = a;
  }
  current->foundAddress = address;
  printPins();
  if (!address) { Serial.println("I2C NO ADDRESS; phase skipped"); return; }
  serviceI2c(500);
  const uint8_t product[] = {0xF9,0};
  sendControl(product,sizeof(product)); serviceI2c(300);
  uint8_t feature[17] = {0xFD,sensor,0,0,0,0xA0,0x86,0x01,0}; // 100000us
  sendControl(feature,sizeof(feature)); serviceI2c(2000);
  const uint8_t query[] = {0xFE,sensor};
  sendControl(query,sizeof(query)); serviceI2c(300);
  for (uint8_t threshold : {uint8_t(0),uint8_t(255)}) {
    // Diagnostic thresholds, printed explicitly; 255 coverage is not assumed.
    uint8_t errors[12] = {0xF2,uint8_t(threshold ? 1 : 0),1,threshold};
    Serial.printf("ERROR QUERY THRESHOLD%u\n", threshold);
    sendControl(errors,sizeof(errors)); serviceI2c(400);
  }
  if (current->inputs < 5) {
    const uint8_t on[] = {5,0,1,executableSeq,2};
    Wire.beginTransmission(address); Wire.write(on,sizeof(on));
    const int status=Wire.endTransmission();
    Serial.printf("EXECUTABLE ON STATUS%d\n",status);
    if (status) ++current->ioErrors; else ++current->tx;
  }
  serviceI2c(2000);
  Serial.printf("I2C RESULT RX%lu INPUT_PKTS%lu BAD%lu IO_ERR%lu\n",
                (unsigned long)current->packets,(unsigned long)current->inputs,
                (unsigned long)current->bad,(unsigned long)current->ioErrors);
}
void printSummary() {
  Serial.println("BUS DIAGNOSTIC SUMMARY BEGIN");
  for (unsigned i=0;i<5;++i) {
    const Result &r=results[i];
    Serial.printf("%u %s ADDR%02X BYTES%lu RX%lu INPUT%lu BAD%lu IO%lu FEATURE%u INTERVAL%lu\n",
                  i,r.name,r.foundAddress,(unsigned long)r.bytes,(unsigned long)r.packets,
                  (unsigned long)r.inputs,(unsigned long)r.bad,(unsigned long)r.ioErrors,
                  r.featureSeen ? 1U:0U,(unsigned long)r.interval);
  }
  Serial.println("BUS DIAGNOSTIC SUMMARY END; motors disabled; buses idle");
  lastSummaryMs=millis();
}
void setup() {
  for (uint8_t p:MOTOR_PINS) { pinMode(p,OUTPUT); digitalWrite(p,LOW); }
  Serial.setTxBufferSize(4096); Serial.begin(115200);
  delay(500);
  Serial.println("FIRMWARE MAKER_IMU_BUS_DIAGNOSTIC_V1; MOTORS DISABLED; P1 MUST BE GND");
  runRvc(0,false,false);
  runRvc(1,true,true);
  runI2c(2,100000,1);
  runI2c(3,100000,2);
  runI2c(4,10000,1);
  stopBuses();
  // Leave P0 low and RST released; no repeated reset or sensor writes.
  pinMode(32,INPUT);
  printSummary();
}
void loop() {
  if (static_cast<uint32_t>(millis()-lastSummaryMs)>=10000) printSummary();
  delay(1);
}
