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
  uint8_t sensor = 0;
  uint32_t requestedInterval = 0;
  uint32_t samples = 0, duplicateInputs = 0, firstSampleMs = 0, lastSampleMs = 0;
  uint8_t lastInputSeq = 0;
  int16_t xyz[3] = {};
  uint8_t firstInput[40] = {}, lastInput[40] = {}, lastControl[48] = {};
  unsigned firstInputSize = 0, lastInputSize = 0, lastControlSize = 0;
  uint8_t controls[6][48] = {};
  unsigned controlSizes[6] = {}, controlCount = 0;
  uint8_t errorRecords[16][6] = {};
  unsigned errorCount = 0, errorEnds = 0;
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
    if (current->inputs && p[3] == current->lastInputSeq) ++current->duplicateInputs;
    current->lastInputSeq = p[3];
    ++current->inputs;
    const unsigned kept = n < 40 ? n : 40;
    if (!current->firstInputSize) {
      memcpy(current->firstInput,p,kept); current->firstInputSize=kept;
    }
    memcpy(current->lastInput,p,kept); current->lastInputSize=kept;
    unsigned pos = 4;
    if (n >= 9 && p[pos] == 0xFB) pos += 5;
    // Only decode the selected single vector report, with a bounded payload.
    if (p[2] == 3 && n-pos >= 10 && p[pos] == current->sensor) {
      if (!current->samples) current->firstSampleMs=millis();
      ++current->samples; current->lastSampleMs=millis();
      for(unsigned axis=0;axis<3;++axis) {
        const unsigned at=pos+4+2*axis;
        current->xyz[axis]=int16_t(uint16_t(p[at]) | (uint16_t(p[at+1])<<8));
      }
    }
    if (current->inputs <= 3) dumpBytes("I2C INPUT", p, n < 40 ? n : 40);
  } else if (current->packets <= 6) {
    dumpBytes("I2C RX", p, n < 48 ? n : 48);
  }
  if (p[2] != 2) return;
  // The I2C header-only peek is followed by a full read. That second header
  // may carry the continuation bit even though all of this payload is here.
  // readPacket checks the original header and matching channel/sequence.
  const unsigned kept = n < 48 ? n : 48;
  memcpy(current->lastControl,p,kept); current->lastControlSize=kept;
  if(current->controlCount<6) {
    memcpy(current->controls[current->controlCount],p,kept);
    current->controlSizes[current->controlCount++]=kept;
  }
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
      if(q[7]==0xFF) ++current->errorEnds;
      else if(current->errorCount<16) memcpy(current->errorRecords[current->errorCount++],q+5,6);
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
  // SHTP 2.3.1 increments sequence on EACH read, including continuation.
  if (actual != n || data[2] > 5 || data[2]!=header[2] || data[3]!=uint8_t(header[3]+1) || !(data[1]&0x80)) { ++current->bad; return false; }
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
void runI2c(unsigned slot, uint32_t clockHz, uint8_t sensor, uint32_t intervalUs = 100000) {
  current = &results[slot];
  current->name = sensor == 1 ? "ACCEL" : sensor == 2 ? "GYRO" : sensor == 7 ? "GYRO_UNCAL" : "GEOMAG_QUAT";
  current->sensor = sensor;
  current->requestedInterval = intervalUs;
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
  uint8_t feature[17] = {0xFD,sensor,0,0,0};
  for(unsigned i=0;i<4;++i) feature[5+i]=uint8_t(intervalUs>>(8*i));
  sendControl(feature,sizeof(feature)); serviceI2c(2000);
  const uint8_t query[] = {0xFE,sensor};
  sendControl(query,sizeof(query)); serviceI2c(300);
  // Finish streaming observation before querying errors to avoid any influence
  // from the diagnostic query on the observed sensor output.
  serviceI2c(30000);
  for (uint8_t threshold : {uint8_t(0),uint8_t(4)}) {
    // Severity 4 was actually observed in V3; avoid the unbounded 255 query.
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
  serviceI2c(1000);
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
    Serial.printf("REQUESTED_US%lu SAMPLES%lu SPAN_MS%lu DUP_INPUT%lu LAST_XYZ_RAW %d %d %d ERRORS%u END_MARKERS%u\n",
                  (unsigned long)r.requestedInterval,
                  (unsigned long)r.samples,(unsigned long)(r.lastSampleMs-r.firstSampleMs),
                  (unsigned long)r.duplicateInputs,r.xyz[0],r.xyz[1],r.xyz[2],r.errorCount,r.errorEnds);
    dumpBytes("FIRST_INPUT",r.firstInput,r.firstInputSize);
    dumpBytes("LAST_INPUT",r.lastInput,r.lastInputSize);
    for(unsigned k=0;k<r.controlCount;++k) dumpBytes("CONTROL",r.controls[k],r.controlSizes[k]);
    dumpBytes("LAST_CONTROL",r.lastControl,r.lastControlSize);
    for(unsigned k=0;k<r.errorCount;++k) dumpBytes("ERROR_R0_R5",r.errorRecords[k],6);
  }
  Serial.println("BUS DIAGNOSTIC SUMMARY END; motors disabled; buses idle");
  lastSummaryMs=millis();
}
void setup() {
  for (uint8_t p:MOTOR_PINS) { pinMode(p,OUTPUT); digitalWrite(p,LOW); }
  Serial.setTxBufferSize(4096); Serial.begin(115200);
  delay(500);
  Serial.println("FIRMWARE MAKER_IMU_BUS_DIAGNOSTIC_V4; MOTORS DISABLED; P1 MUST BE GND");
  runI2c(0,100000,2,20000);
  runI2c(1,100000,2,5000);
  runI2c(2,100000,7,20000);
  runI2c(3,100000,9);
  runI2c(4,100000,1);
  stopBuses();
  // Leave P0 low and RST released; no repeated reset or sensor writes.
  pinMode(32,INPUT);
  printSummary();
}
void loop() {
  if (static_cast<uint32_t>(millis()-lastSummaryMs)>=10000) printSummary();
  delay(1);
}
