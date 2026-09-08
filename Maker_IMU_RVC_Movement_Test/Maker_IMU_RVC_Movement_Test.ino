// Maker ESP32 Pro: continuous RVC diagnostics during repeating mecanum motion.
// Level converter LV1 -> GPIO21; IMU VDC/P0 stay at 5V. No IMU reset control.
// GO + newline starts a countdown. X stops immediately. Boot is always idle.
#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp32-hal-uart.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include "RvcParser.h"
#include "MotorSafety.h"

#if !defined(CONFIG_IDF_TARGET_ESP32) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "Select ESP32 Dev Module for NULLLAB Maker ESP32 Pro, ESP32 core 3.x."
#endif

constexpr uint8_t MOTOR_PINS[] = {27, 13, 4, 2, 17, 12, 14, 15};
constexpr uint32_t PWM_HZ = 248;
constexpr uint8_t TEST_PWM = 150;  // Logical 8-bit units; hardware duty300/512 = 58.6%.
constexpr uint8_t PWM_BITS = 9;   // APB80MHz needs 9 bits to reach 248Hz.
constexpr uint32_t MOVE_MS = 1000, COAST_MS = 300, COUNTDOWN_MS = 3000;
static_assert(COAST_MS >= MotorSafety::REVERSE_PAUSE_MS, "Keep the reversal coast interval");
// FL=M2, FR=M3, RL=M1, RR=M0. Logical + means wheel-forward.
constexpr uint8_t DRIVE_PINS[4][2] = {{17,12},{14,15},{4,2},{27,13}};
constexpr int8_t MOTOR_SIGN[4] = {1,1,1,-1};
constexpr int8_t MOVE_SIGNS[4][4] = {{1,1,1,1},{-1,-1,-1,-1},{-1,1,1,-1},{1,-1,-1,1}};
const char* const MOVE_NAMES[] = {"FORWARD","BACKWARD","STRAFE_LEFT","STRAFE_RIGHT"};
enum class MotionState { IDLE, COUNTDOWN, MOVING, COAST, FAULT };
MotionState motionState = MotionState::IDLE;
uint8_t moveIndex = 0;
uint32_t phaseSinceMs = 0, cycles = 0;
const char* stopReason = "BOOT_IDLE";
char commandLine[16] = {};
size_t commandLength = 0;
bool discardCommand = false;
bool motorReady = false, motorAttached[4][2] = {};
SemaphoreHandle_t motorMutex = nullptr;
MotorSafety::Controller outputController;
// Access these only with motorMutex once the output task exists.
bool motorWatchdogTripped = false, motorWriteFailed = false;
bool moveWindowActive = false;
uint32_t moveWindowSinceMs = 0;

void zeroMotorOutputs() {
  for (unsigned i=0;i<4;++i) for (unsigned j=0;j<2;++j) {
    if (motorAttached[i][j]) {
      if (!ledcWrite(DRIVE_PINS[i][j],0)) motorWriteFailed=true;
    } else digitalWrite(DRIVE_PINS[i][j],LOW);
  }
}

void writeMotorDuty(unsigned i,float logical) {
  const float electrical=logical*MOTOR_SIGN[i];
  const uint16_t duty=static_cast<uint16_t>(lroundf(fabsf(electrical)))*2;
  const unsigned active=electrical>0 ? 0 : 1;
  // Clear the opposite bridge input before applying PWM.
  if (!ledcWrite(DRIVE_PINS[i][1-active],0) ||
      !ledcWrite(DRIVE_PINS[i][active],duty)) motorWriteFailed=true;
}

// Called under the mutex by the independent output task (also host-testable).
void serviceMotorOutputs(uint32_t now) {
  // The task enforces each one-second limit even if logging/main loop blocks.
  if (moveWindowActive && now-moveWindowSinceMs>=MOVE_MS) {
    outputController.stop(now);
    moveWindowActive=false;
  }
  if (outputController.tick(now)) motorWatchdogTripped=true;
  if (motorWatchdogTripped || motorWriteFailed) {
    outputController.stop(now);
    moveWindowActive=false;
  }
  for (unsigned i=0;i<4;++i) writeMotorDuty(i,outputController.wheels[i].applied);
  if (motorWriteFailed) { outputController.stop(now); zeroMotorOutputs(); }
}

void motorOutputTask(void*) {
  TickType_t wake=xTaskGetTickCount();
  while (true) {
    xSemaphoreTake(motorMutex,portMAX_DELAY);
    serviceMotorOutputs(millis());
    xSemaphoreGive(motorMutex);
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(5));
  }
}

bool setupMotors() {
  for (uint8_t pin:MOTOR_PINS) { digitalWrite(pin,LOW); pinMode(pin,OUTPUT); digitalWrite(pin,LOW); }
  motorMutex=xSemaphoreCreateMutex();
  if (!motorMutex) return false;
  // Classic ESP32 has no LEDC XTAL source; APB80MHz at 248Hz needs 9 bits.
  if (!ledcSetClockSource(LEDC_USE_APB_CLK)) return false;
  bool attached=true;
  for (unsigned i=0;i<4;++i) for (unsigned j=0;j<2;++j) {
    motorAttached[i][j]=ledcAttachChannel(DRIVE_PINS[i][j],PWM_HZ,PWM_BITS,i*2+j);
    attached=attached && motorAttached[i][j];
  }
  zeroMotorOutputs();
  if (!attached || motorWriteFailed) return false;
  outputController.lastTick=millis();
  return xTaskCreate(motorOutputTask,"rvc_motors",3072,nullptr,3,nullptr)==pdPASS;
}

bool motionActive() {
  return motionState==MotionState::COUNTDOWN || motionState==MotionState::MOVING || motionState==MotionState::COAST;
}

const char* motionName() {
  switch (motionState) {
    case MotionState::COUNTDOWN: return "COUNTDOWN";
    case MotionState::MOVING: return "MOVING";
    case MotionState::COAST: return "COAST";
    case MotionState::FAULT: return "FAULT";
    default: return "IDLE";
  }
}

void stopMotion(const char* reason,bool fault) {
  if (motorMutex) xSemaphoreTake(motorMutex,portMAX_DELAY);
  outputController.stop(millis());
  moveWindowActive=false;
  zeroMotorOutputs();
  const bool resetRequired=!motorReady || motorWriteFailed;
  if (motorMutex) xSemaphoreGive(motorMutex);
  motionState=fault ? MotionState::FAULT : MotionState::IDLE;
  stopReason=reason;
  Serial.printf("EVENT MOTION_STOP boot_ms=%lu reason=%s; %s\n",(unsigned long)millis(),reason,
    resetRequired ? "correct motor fault and reset board before GO" : "send GO to restart");
}

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
  Serial.println("MAKER_IMU_RVC_MOVEMENT V1 | UART1 RX21 TX=none | 115200 8N1");
  Serial.println("GO + newline: start after 3s countdown. X: immediate stop. s: snapshot. ?: help.");
  Serial.println("Repeat FORWARD, BACKWARD, STRAFE_LEFT, STRAFE_RIGHT: 1000ms each, 300ms coast between.");
  Serial.printf("PWM: APB %luHz, logical duty %u (hardware %u/512); ramp-up is included in each 1000ms motion window.\n",(unsigned long)PWM_HZ,TEST_PWM,TEST_PWM*2);
  Serial.println("READY requires 5 sequential checksum-valid frames, <=100ms apart; STALE after >500ms without a valid frame.");
  Serial.println("Boot stays stopped. Stale IMU or motor-task command timeout latches a stop; fresh GO required.");
  Serial.println("No heading correction, encoders, IMU reset or power switching. Use existing 5V IMU/level-converter wiring.");
  Serial.println("boot_ms is ESP uptime, not measured IMU power-on time. Index discontinuities are not exact lost-frame counts.");
}

void stats(uint32_t now) {
  float applied[4] = {};
  if (motorMutex) xSemaphoreTake(motorMutex,portMAX_DELAY);
  for (unsigned i=0;i<4;++i) applied[i]=outputController.wheels[i].applied;
  if (motorMutex) xSemaphoreGive(motorMutex);
  Serial.printf("MOTION boot_ms=%lu state=%s direction=%s cycle=%lu phase_ms=%lu applied_fl_fr_rl_rr=%.0f,%.0f,%.0f,%.0f reason=%s\n",
    (unsigned long)now,motionName(),MOVE_NAMES[moveIndex],(unsigned long)cycles,
    (unsigned long)(motionActive() ? now-phaseSinceMs : 0),applied[0],applied[1],applied[2],applied[3],stopReason);
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
  Serial.printf("UART_EVENTS total/post_first_ready fifo,buffer,frame,parity,break=");
  const unsigned order[]={UART_FIFO_OVF_ERROR,UART_BUFFER_FULL_ERROR,UART_FRAME_ERROR,UART_PARITY_ERROR,UART_BREAK_ERROR};
  for (unsigned i:order) {
    const uint32_t n=errors[i].load(std::memory_order_relaxed);
    Serial.printf(" %lu/%lu", (unsigned long)n, (unsigned long)(acquisitions ? n-errorBaseline[i] : 0));
  }
  Serial.println("");
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
  if (motionActive()) stopMotion("IMU_STALE",true);
  Serial.printf("EVENT STALE boot_ms=%lu age_ms=%lu\n",(unsigned long)now,(unsigned long)(now-lastFrameMs));
}

void startMotion(uint32_t now) {
  if (motionActive()) { Serial.println("GO ignored: test already running; X stops."); return; }
  if (!motorReady) { Serial.println("GO rejected: motor initialization failed."); return; }
  if (!uartReady || !ready || now-lastFrameMs>QUALIFY_GAP_MS) {
    Serial.println("GO rejected: wait for a fresh READY IMU stream, then send GO again.");
    return;
  }
  xSemaphoreTake(motorMutex,portMAX_DELAY);
  const bool failed=motorWriteFailed;
  if (!failed) motorWatchdogTripped=false;  // Manual rearm only.
  outputController.stop(now);
  moveWindowActive=false;
  zeroMotorOutputs();
  xSemaphoreGive(motorMutex);
  if (failed) { stopMotion("PWM_WRITE_FAILED",true); return; }
  moveIndex=0;
  cycles=1;
  phaseSinceMs=now;
  motionState=MotionState::COUNTDOWN;
  stopReason="NONE";
  Serial.println("EVENT COUNTDOWN: motion starts in 3 seconds; X cancels.");
}

bool publishMotion(uint32_t now,bool newWindow) {
  float target[4];
  for (unsigned i=0;i<4;++i) target[i]=MOVE_SIGNS[moveIndex][i]*TEST_PWM;
  xSemaphoreTake(motorMutex,portMAX_DELAY);
  bool ok=!motorWatchdogTripped && !motorWriteFailed;
  if (ok && newWindow) { moveWindowActive=true; moveWindowSinceMs=now; }
  if (ok && moveWindowActive && now-moveWindowSinceMs<MOVE_MS)
    ok=outputController.publish(target,now);
  xSemaphoreGive(motorMutex);
  return ok;
}

void beginMove(uint32_t now) {
  phaseSinceMs=now;
  motionState=MotionState::MOVING;
  if (!publishMotion(now,true)) { stopMotion("MOTOR_FAULT",true); return; }
  Serial.printf("EVENT MOVE boot_ms=%lu direction=%s cycle=%lu duration_ms=%lu\n",
    (unsigned long)now,MOVE_NAMES[moveIndex],(unsigned long)cycles,(unsigned long)MOVE_MS);
}

void updateMotion(uint32_t now) {
  if (!motionActive()) return;
  if (!ready || now-lastFrameMs>STALE_MS) { stopMotion("IMU_STALE",true); return; }
  xSemaphoreTake(motorMutex,portMAX_DELAY);
  const bool watchdog=motorWatchdogTripped, failed=motorWriteFailed;
  xSemaphoreGive(motorMutex);
  if (watchdog || failed) { stopMotion(failed ? "PWM_WRITE_FAILED" : "OUTPUT_WATCHDOG",true); return; }
  const uint32_t elapsed=now-phaseSinceMs;
  if (motionState==MotionState::COUNTDOWN) {
    if (elapsed>=COUNTDOWN_MS) beginMove(now);
  } else if (motionState==MotionState::MOVING) {
    if (elapsed>=MOVE_MS) {
      xSemaphoreTake(motorMutex,portMAX_DELAY);
      outputController.stop(now);
      moveWindowActive=false;
      zeroMotorOutputs();
      xSemaphoreGive(motorMutex);
      motionState=MotionState::COAST;
      phaseSinceMs=now;
      Serial.printf("EVENT COAST boot_ms=%lu after=%s duration_ms=%lu\n",
        (unsigned long)now,MOVE_NAMES[moveIndex],(unsigned long)COAST_MS);
    } else if (!publishMotion(now,false)) stopMotion("MOTOR_FAULT",true);
  } else if (motionState==MotionState::COAST && elapsed>=COAST_MS) {
    moveIndex=(moveIndex+1)%4;
    if (!moveIndex) ++cycles;
    beginMove(now);
  }
}

void readCommands() {
  // Bounded work; UART sensor reads and the motion scheduler keep running.
  for (unsigned n=0;n<64 && Serial.available();++n) {
    const char c=static_cast<char>(Serial.read());
    if (c=='X' || c=='x') {
      stopMotion("USER_X",false);
      commandLength=0;
      discardCommand=true;  // Remaining characters on this line cannot rearm.
    } else if (c=='\r' || c=='\n') {
      if (!discardCommand && commandLength) {
        commandLine[commandLength]='\0';
        if (!strcmp(commandLine,"GO")) startMotion(millis());
        else stopMotion("UNKNOWN_COMMAND",false);
      }
      commandLength=0;
      discardCommand=false;
    } else if (!discardCommand) {
      if (c=='?' && commandLength==0) help();
      else if ((c=='s' || c=='S') && commandLength==0) stats(millis());
      else if (c!=' ' && c!='\t') {
        if (commandLength+1<sizeof(commandLine))
          commandLine[commandLength++]=(c>='a' && c<='z') ? c-'a'+'A' : c;
        else { stopMotion("COMMAND_TOO_LONG",false); commandLength=0; discardCommand=true; }
      }
    }
  }
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
  motorReady=setupMotors();
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
  Serial.printf("MOTOR_CHECK ok=%u outputs=ZERO; GO required before any movement\n",motorReady);
  Serial.printf("UART_CHECK ok=%u rx=%d tx=%d buffer=%u listening_boot_ms=%lu\n",uartReady,rx,tx,(unsigned)allocated,(unsigned long)startMs);
}

void loop() {
  checkStale(millis());
  readCommands();
  updateMotion(millis());
  for (unsigned i=0;uartReady && i<1024 && Serial1.available();++i) {
    const int byte=Serial1.read();
    if (byte>=0) consume(static_cast<uint8_t>(byte),millis());
  }
  uint32_t now=millis();
  updateMotion(now);
  if (!milestone && now-startMs>=900000) {
    milestone=true;
    Serial.println("MILESTONE 15_MINUTES_ELAPSED: inspect counters; this is not an automatic PASS. Listening continues.");
  }
  if (now-lastStatusMs>=1000) {
    stats(now);
    priorFrames=parser.validFrames();
    lastStatusMs=now;
  }
  delay(1);
}
