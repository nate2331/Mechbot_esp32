// Passive continuous RVC test. Existing 3V3 LVC output -> GPIO21.
// IMU VDC/P0 remain on external 5V. No host reset, mode, or power control.
#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp32-hal-uart.h>
#include <atomic>
#include "RvcParser.h"

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "Select ESP32 Dev Module for NULLLAB Maker ESP32 Pro."
#endif

constexpr uint8_t MOTOR_PINS[] = {27, 13, 4, 2, 17, 12, 14, 15};
constexpr uint32_t STALE_MS = 500, QUALIFY_GAP_MS = 100;
RvcParser parser;
std::atomic<uint32_t> errors[6] = {};
uint32_t errorBaseline[6] = {};
uint32_t startMs, lastStatusMs, lastFrameMs, firstFrameMs, firstReadyMs;
uint32_t bytesRead, priorFrames, streak, acquisitions, pauses, maxGapMs;
uint32_t checksumBaseline, gapBaseline, readySinceMs, longestReadyMs;
uint32_t repeats, orientationChanges, accelChanges;
bool uartReady = false, ready = false, stale = false, milestone = false;
RvcParser::Sample previous;

void onError(hardwareSerial_error_t error) {
  const unsigned i = static_cast<unsigned>(error);
  if (i > 0 && i < 6) errors[i].fetch_add(1, std::memory_order_relaxed);
}

void help() {
  Serial.println("MAKER_IMU_RVC_CONTINUOUS V1 | UART1 RX21 TX=none | 115200 8N1");
  Serial.println("AUTO LISTEN: IMU 5V may stay connected. No arming or 35s cutoff.");
  Serial.println("READY requires 5 sequential checksum-valid frames, <=100ms apart; STALE after >500ms without a valid frame.");
  Serial.println("s=snapshot, ?=help. No software reset/power control. Motor outputs held LOW.");
  Serial.println("boot_ms is ESP uptime, not measured IMU power-on time. Index discontinuities are not exact lost-frame counts.");
}

void stats(uint32_t now) {
  const uint32_t frames = parser.validFrames();
  const uint32_t currentReady = ready ? now - readySinceMs : 0;
  Serial.printf("RUN boot_ms=%lu state=%s bytes=%lu frames=%lu new=%lu window_ms=%lu age_ms=%ld first_frame_boot_ms=%lu first_ready_boot_ms=%lu acquisitions=%lu pauses=%lu max_gap_ms=%lu ready_ms=%lu longest_ready_ms=%lu\n",
    (unsigned long)now, !uartReady ? "UART_FAILED" : ready ? "READY" : stale ? "STALE" : "SYNCING",
    (unsigned long)bytesRead, (unsigned long)frames, (unsigned long)(frames-priorFrames),
    (unsigned long)(now-lastStatusMs), frames ? (long)(now-lastFrameMs) : -1L,
    (unsigned long)firstFrameMs, (unsigned long)firstReadyMs, (unsigned long)acquisitions,
    (unsigned long)pauses, (unsigned long)maxGapMs, (unsigned long)currentReady,
    (unsigned long)max(longestReadyMs, currentReady));
  Serial.printf("COUNTS bad_checksum=%lu discontinuities=%lu repeats=%lu post_first_ready_bad=%lu post_first_ready_discontinuities=%lu\n",
    (unsigned long)parser.badChecksums(), (unsigned long)parser.indexDiscontinuities(), (unsigned long)repeats,
    (unsigned long)(acquisitions ? parser.badChecksums()-checksumBaseline : 0),
    (unsigned long)(acquisitions ? parser.indexDiscontinuities()-gapBaseline : 0));
  Serial.print("UART_EVENTS total/post_first_ready fifo,buffer,frame,parity,break=");
  const unsigned order[]={UART_FIFO_OVF_ERROR,UART_BUFFER_FULL_ERROR,UART_FRAME_ERROR,UART_PARITY_ERROR,UART_BREAK_ERROR};
  for (unsigned i:order) {
    const uint32_t n=errors[i].load(std::memory_order_relaxed);
    Serial.printf(" %lu/%lu", (unsigned long)n, (unsigned long)(acquisitions ? n-errorBaseline[i] : 0));
  }
  Serial.println();
  if (frames) {
    const auto& s=parser.sample();
    Serial.printf("VALUE ypr_deg=%.2f,%.2f,%.2f accel_mg=%d,%d,%d changes_ypr=%lu changes_accel=%lu\n",
      s.yaw*.01,s.pitch*.01,s.roll*.01,s.ax,s.ay,s.az,
      (unsigned long)orientationChanges,(unsigned long)accelChanges);
  }
}

void checkStale(uint32_t now) {
  if (!parser.validFrames() || stale || now-lastFrameMs<=STALE_MS) return;
  stale=true;
  streak=0;
  if (ready) longestReadyMs=max(longestReadyMs,lastFrameMs-readySinceMs);
  ready=false;
  ++pauses;
  Serial.printf("EVENT STALE boot_ms=%lu age_ms=%lu\n",(unsigned long)now,(unsigned long)(now-lastFrameMs));
}

void consume(uint8_t byte,uint32_t now) {
  ++bytesRead;
  const uint32_t before=parser.validFrames(), bad=parser.badChecksums();
  if (!parser.feed(byte)) {
    if (parser.badChecksums()!=bad) streak=0;
    return;
  }
  const auto& s=parser.sample();
  const uint32_t gap=now-lastFrameMs;
  const bool consecutive=before && static_cast<uint8_t>(s.index-previous.index)==1;
  if (!before) firstFrameMs=now;
  else {
    maxGapMs=max(maxGapMs,gap);
    if (s.index==previous.index) ++repeats;
    if (s.yaw!=previous.yaw || s.pitch!=previous.pitch || s.roll!=previous.roll) ++orientationChanges;
    if (s.ax!=previous.ax || s.ay!=previous.ay || s.az!=previous.az) ++accelChanges;
  }
  streak=consecutive && gap<=QUALIFY_GAP_MS ? min(streak+1,5UL) : 1;
  previous=s;
  lastFrameMs=now;
  if (!ready && streak>=5) {
    if (!acquisitions) {
      firstReadyMs=now;
      checksumBaseline=parser.badChecksums();
      gapBaseline=parser.indexDiscontinuities();
      for (unsigned i=1;i<6;++i) errorBaseline[i]=errors[i].load(std::memory_order_relaxed);
    }
    ++acquisitions;
    ready=true;
    stale=false;
    readySinceMs=now;
    Serial.printf("EVENT READY boot_ms=%lu acquisition=%lu\n",(unsigned long)now,(unsigned long)acquisitions);
  }
}

void setup() {
  for (uint8_t pin:MOTOR_PINS) { digitalWrite(pin,LOW); pinMode(pin,OUTPUT); digitalWrite(pin,LOW); }
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  pinMode(21,INPUT);
  const size_t allocated=Serial1.setRxBufferSize(8192);
  Serial1.begin(115200,SERIAL_8N1,21,-1);
  const int rx=uart_get_RxPin(1),tx=uart_get_TxPin(1);
  uartReady=Serial1 && allocated>=8192 && rx==21 && tx==-1;
  if (uartReady) Serial1.onReceiveError(onError);
  else { Serial1.end(); pinMode(21,INPUT); }
  startMs=lastStatusMs=millis();
  help();
  Serial.printf("UART_CHECK ok=%u rx=%d tx=%d buffer=%u listening_boot_ms=%lu\n",uartReady,rx,tx,(unsigned)allocated,(unsigned long)startMs);
}

void loop() {
  checkStale(millis());
  for (unsigned i=0;uartReady && i<1024 && Serial1.available();++i) {
    const int byte=Serial1.read();
    if (byte>=0) consume(static_cast<uint8_t>(byte),millis());
  }
  uint32_t now=millis();
  if (!milestone && now-startMs>=900000) {
    milestone=true;
    Serial.println("MILESTONE 15_MINUTES_ELAPSED: inspect counters; this is not an automatic PASS. Listening continues.");
  }
  if (Serial.available()) {
    char c=Serial.read();
    if (c=='s') stats(now);
    else if (c=='?') help();
  }
  if (now-lastStatusMs>=1000) {
    stats(now);
    priorFrames=parser.validFrames();
    lastStatusMs=now;
  }
  delay(1);
}
