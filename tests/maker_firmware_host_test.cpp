// Compile the actual firmware against fake IO; never opens serial hardware.
#include <cassert>
#include <iostream>
#include <vector>
#include "../Maker_Mechbot/Maker_Mechbot.ino"

void outputTick() {
  if (outputController.tick(millis())) motorWatchdogTripped=true;
  for (uint8_t i=0;i<MOTOR_COUNT;++i) writeMotorDuty(i,outputController.wheels[i].applied);
}
void cmd(const char* text) {
  std::vector<char> line(text,text+std::strlen(text)+1);
  processCommand(line.data());
}
void zero() {
  for (const auto& m: motors) assert(pinDuty[m.dir1Pin]==0 && pinDuty[m.dir2Pin]==0);
}
void powerFor(unsigned ms) {
  for(unsigned t=0;t<ms;t+=5) { fakeNow+=5; outputTick(); }
}
void receive(const std::string& input) {
  Serial.input=input;
  while(Serial.available()) receiveSerialCommands();
}
void assertImuResetState() {
  zero();
  assert(pinDuty[IMU_WAKE_PIN]==HIGH);
  assert(!outputController.live && !commandActive && !motionRequested);
  for(const auto command: pendingMotorCommands) assert(command==0);
  assert(!headingTargetValid && !fieldOrientedEnabled && !fieldReferenceValid);
  assert(!imuAvailable && !imuQuaternionValid && !imuGyroValid && !imuAccelerationValid);
  assert(lastImuEventMs==0 && lastImuQuaternionMs==0);
  assert(lastImuGyroMs==0 && lastImuAccelerationMs==0);
}
void testImuHardwareResetRecovery() {
  // A failed manual retry must stop existing motion and invalidate old samples
  // before touching reset, including when SPI initialization cannot succeed.
  imuAvailable=imuQuaternionValid=imuGyroValid=imuAccelerationValid=true;
  imuQx=imuQy=imuQz=0; imuQw=1; imuStatus=3;
  lastImuEventMs=lastImuQuaternionMs=lastImuGyroMs=lastImuAccelerationMs=millis();
  cmd("F 1");
  assert(fieldOrientedEnabled && fieldReferenceValid);
  cmd("V 1 0 0"); powerFor(20);
  assert(pinDuty[motors[FRONT_LEFT].dir1Pin]>0);
  headingTargetValid=true;
  bno08x.beginSucceeds=false;
  bno08x.needsHardwareReset=true;
  bno08x.initializationCalls.clear();
  const unsigned failedResetCount=bno08x.hardwareResetCalls;
  const unsigned failedBeginCount=bno08x.beginCalls;
  cmd("IMU RETRY");
  assert(bno08x.initializationCalls=="BR");
  assert(bno08x.hardwareResetCalls==failedResetCount+1);
  assert(bno08x.beginCalls==failedBeginCount+1);
  assert(!fakeShtpSlot && !fakeSh2Session && fakeSh2NullCloseCalls==0);
  assertImuResetState();
  Serial.output.clear(); sendImuTelemetry();
  assert(Serial.output.find("OFFLINE")!=std::string::npos);

  // Offline polling must not reset a sensor while motion is requested.
  bno08x.beginSucceeds=true;
  bno08x.needsHardwareReset=true;
  fakeNow+=IMU_RETRY_INTERVAL_MS;
  cmd("V 1 0 0"); powerFor(20);
  const unsigned stoppedResetCount=bno08x.hardwareResetCalls;
  pollImu();
  assert(bno08x.hardwareResetCalls==stoppedResetCount);
  assert(!imuAvailable);

  // Once stopped, the same offline retry must reset and initialize SPI again.
  cmd("X");
  bno08x.initializationCalls.clear();
  pollImu();
  assert(bno08x.initializationCalls=="BR");
  assert(bno08x.hardwareResetCalls==stoppedResetCount+1);
  assert(imuAvailable);
  assert(fakeShtpSlot==bno08x.testHal() && fakeSh2Session==bno08x.testHal());
  assert(fakeSh2Allocations==fakeSh2CloseCalls+1);
  assert(fakeSh2NullCloseCalls==0 && fakeBeginWithLiveSession==0);
  zero();
  assert(!outputController.live && !commandActive && !motionRequested);
  assert(!headingTargetValid && !fieldOrientedEnabled && !fieldReferenceValid);
  assert(!imuQuaternionValid && !imuGyroValid && !imuAccelerationValid);
  assert(lastImuEventMs==0 && lastImuQuaternionMs==0);
  assert(lastImuGyroMs==0 && lastImuAccelerationMs==0);
  Serial.output.clear(); sendImuTelemetry();
  assert(Serial.output.find("WAIT")!=std::string::npos);

  // Bus setup can fail before the adapter opens. Preserve stopped/invalid
  // firmware state, then allow the next retry to reclaim the previous session.
  imuQuaternionValid=imuGyroValid=imuAccelerationValid=true;
  lastImuEventMs=lastImuQuaternionMs=lastImuGyroMs=lastImuAccelerationMs=millis();
  cmd("V 1 0 0"); powerFor(20);
  SPI.beginSucceeds=false;
  const unsigned resetsBeforeBusFailure=bno08x.hardwareResetCalls;
  const unsigned beginsBeforeBusFailure=bno08x.beginCalls;
  cmd("IMU RETRY");
  assertImuResetState();
  assert(bno08x.hardwareResetCalls==resetsBeforeBusFailure);
  assert(bno08x.beginCalls==beginsBeforeBusFailure);
  SPI.beginSucceeds=true;
  cmd("IMU RETRY");
  assert(imuAvailable && !imuQuaternionValid && !imuGyroValid && !imuAccelerationValid);
  zero();
  assert(fakeSh2Allocations==fakeSh2CloseCalls+1);
  assert(fakeSh2NullCloseCalls==0 && fakeBeginWithLiveSession==0);
}

void receivedImuReport(unsigned id) {
  bno08x.nextEvent=sh2_SensorValue_t{};
  bno08x.nextEvent.sensorId=id;
  bno08x.eventPending=true;
  pollImu();
}
void receivedAllImuReports() {
  receivedImuReport(SH2_GAME_ROTATION_VECTOR);
  receivedImuReport(SH2_GYROSCOPE_CALIBRATED);
  receivedImuReport(SH2_LINEAR_ACCELERATION);
}
void assertStoppedReportRequest() {
  zero();
  assert(!outputController.live && !motionRequested && !commandActive);
  assert(!headingTargetValid && !fieldOrientedEnabled && !fieldReferenceValid);
}
void testStalledImuRecovery() {
  fakeNow=100000;
  cmd("X");
  pinInput[IMU_INT_PIN]=LOW;
  bno08x.enableReportHook=assertStoppedReportRequest;
  initializeImu();
  receivedAllImuReports();
  const uint32_t lastSample=millis();
  const unsigned begins=bno08x.beginCalls;
  const unsigned requests=bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR];
  // Reproduce the reported failure: all validity flags stay true, but the
  // quaternion timestamp never advances. A stale stream must now recover.
  fakeNow+=600; pollImu();
  assert(imuQuaternionValid && imuGyroValid && imuAccelerationValid);
  assert(lastImuQuaternionMs==lastSample);
  Serial.output.clear(); sendImuTelemetry();
  assert(Serial.output.find("STALE")!=std::string::npos);
  fakeNow+=1999; pollImu();
  assert(bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR]==requests);
  fakeNow+=1;
  const uint32_t firstRetry=millis();
  pollImu();
  assert(bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR]==requests+1);
  assert(bno08x.beginCalls==begins);
  const unsigned retries=imuReportRetryCount;
  for(unsigned n=0;n<20;++n) pollImu();
  assert(imuReportRetryCount==retries); // Fast polling cannot flood requests.
  fakeNow=firstRetry+1000; pollImu();
  assert(imuReportRetryCount==retries+1);
  fakeNow=firstRetry+2000; pollImu();
  fakeNow=firstRetry+2999; pollImu();
  assert(bno08x.beginCalls==begins);
  fakeNow=firstRetry+3000; pollImu();
  assert(bno08x.beginCalls==begins+1);
  assert(imuAvailable && !imuQuaternionValid && !imuGyroValid && !imuAccelerationValid);
  assertStoppedReportRequest();
  // An unresolved startup gets a new grace period, not a tight reset loop.
  for(unsigned n=0;n<20;++n) pollImu();
  assert(bno08x.beginCalls==begins+1);

  // Receiving all streams cancels escalation. A later stall starts afresh.
  receivedAllImuReports();
  fakeNow+=600; pollImu();
  fakeNow+=2000; pollImu();
  assert(imuReportsRetried);
  receivedAllImuReports();
  assert(!imuReportsUnhealthy && !imuReportsRetried);
  fakeNow+=3500; receivedAllImuReports();
  assert(bno08x.beginCalls==begins+1);
  fakeNow+=600; pollImu();
  fakeNow+=1999; pollImu();
  assert(bno08x.beginCalls==begins+1 && !imuReportsRetried);
  bno08x.enableReportHook=nullptr;
}
void testMissingReportAndMotionDeferral() {
  cmd("X"); initializeImu();
  const unsigned q=bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR];
  const unsigned g=bno08x.enableReportCalls[SH2_GYROSCOPE_CALIBRATED];
  const unsigned a=bno08x.enableReportCalls[SH2_LINEAR_ACCELERATION];
  receivedImuReport(SH2_GAME_ROTATION_VECTOR);
  receivedImuReport(SH2_LINEAR_ACCELERATION);
  // Keep two streams live while gyro never arrives, through initial grace.
  for(unsigned n=0;n<5;++n) {
    fakeNow+=400;
    receivedImuReport(SH2_GAME_ROTATION_VECTOR);
    receivedImuReport(SH2_LINEAR_ACCELERATION);
  }
  assert(bno08x.enableReportCalls[SH2_GAME_ROTATION_VECTOR]==q);
  assert(bno08x.enableReportCalls[SH2_GYROSCOPE_CALIBRATED]==g+1);
  assert(bno08x.enableReportCalls[SH2_LINEAR_ACCELERATION]==a);
  receivedImuReport(SH2_GYROSCOPE_CALIBRATED);
  assert(!imuReportsUnhealthy && !imuReportsRetried);

  // Old valid samples do not trigger any automatic writes while moving.
  const unsigned before=bno08x.beginCalls, retryBefore=imuReportRetryCount;
  fakeNow+=600;
  cmd("V 1 0 0"); powerFor(20); pollImu();
  for(unsigned n=0;n<6;++n) {
    fakeNow+=1000;
    cmd("V 1 0 0"); pollImu();
  }
  assert(motionRequested && bno08x.beginCalls==before && imuReportRetryCount==retryBefore);
  // A zero motion command may leave ramped power momentarily; recovery must
  // zero it immediately before the first report write, then allow its grace.
  cmd("V 0 0 0");
  assert(!motionRequested && pinDuty[motors[FRONT_LEFT].dir1Pin]>0);
  bno08x.enableReportHook=assertStoppedReportRequest;
  const uint32_t retryStart=millis();
  pollImu();
  assert(imuReportRetryCount==retryBefore+1 && bno08x.beginCalls==before);
  fakeNow=retryStart+2000; pollImu();
  fakeNow=retryStart+2999; pollImu();
  assert(bno08x.beginCalls==before);
  fakeNow=retryStart+3000; pollImu();
  assert(bno08x.beginCalls==before+1);
  bno08x.enableReportHook=nullptr;
}
void testImuRecoveryRolloverAndDiagnostics() {
  cmd("X");
  fakeNow=UINT32_MAX-2500;
  initializeImu(); receivedAllImuReports();
  const unsigned before=bno08x.beginCalls;
  fakeNow+=600; pollImu();
  fakeNow+=1999; pollImu();
  assert(!imuReportsRetried);
  fakeNow+=1;
  const uint32_t retryStart=millis();
  pollImu();
  assert(imuReportsRetried && bno08x.beginCalls==before);
  fakeNow=retryStart+3000; pollImu();
  assert(bno08x.beginCalls==before+1);

  // Snapshot uses wrap-safe millisecond ages and -1 for missing/invalid data.
  fakeNow=25;
  imuQuaternionValid=imuGyroValid=true; imuAccelerationValid=false;
  lastImuQuaternionMs=UINT32_MAX-74; lastImuGyroMs=5;
  pinInput[IMU_INT_PIN]=HIGH;
  const unsigned diagBegins=bno08x.beginCalls, diagRetries=imuReportRetryCount;
  Serial.output.clear(); cmd("IMU DIAG");
  assert(Serial.output.find("IMU DIAG SPI AVAILABLE 1 INT 1 Q 1 AGE 100 G 1 AGE 20 A 0 AGE -1")!=std::string::npos);
  assert(bno08x.beginCalls==diagBegins && imuReportRetryCount==diagRetries);
  assert(imuQuaternionValid && imuGyroValid && !imuAccelerationValid);
  cmd("V 1 0 0"); powerFor(20);
  Serial.output.clear(); cmd("IMU DIAG");
  assert(Serial.output.find("ERR IMU DIAG requires stopped motion")!=std::string::npos);
  assert(motionRequested && outputController.live);
  assert(bno08x.beginCalls==diagBegins && imuReportRetryCount==diagRetries);
  cmd("X");
  Serial.output.clear(); cmd("?");
  assert(Serial.output.find("FIRMWARE MAKER_SPI_V2_STALE_RECOVERY")!=std::string::npos);
}
void testQueuedImuArrivalFreshness() {
  cmd("X");
  fakeNow=200000;
  pinInput[IMU_INT_PIN]=LOW;
  initializeImu();
  assert(fakeSh2SensorCallback);
  const uint32_t arrival=millis();
  // One sensor packet may produce all three callbacks before the firmware
  // drains them. Their ages must reflect receipt, not later queue consumption.
  for(unsigned id: {SH2_GAME_ROTATION_VECTOR,SH2_GYROSCOPE_CALIBRATED,SH2_LINEAR_ACCELERATION}) {
    sh2_SensorEvent_t event;
    event.decoded.sensorId=id;
    fakeSh2SensorCallback(fakeSh2SensorCookie,&event);
  }
  fakeNow+=700;
  pollImu();
  assert(imuQuaternionValid && imuGyroValid && imuAccelerationValid);
  assert(lastImuQuaternionMs==arrival && lastImuGyroMs==arrival && lastImuAccelerationMs==arrival);
  float yaw=0;
  assert(!readCurrentYaw(yaw));
  Serial.output.clear(); sendImuTelemetry();
  assert(Serial.output.find("STALE")!=std::string::npos);
  receivedAllImuReports();
  assert(readCurrentYaw(yaw));
  assert(!imuReportsUnhealthy && !imuReportsRetried);
  Serial.output.clear(); sendImuTelemetry();
  assert(Serial.output.find("STALE")==std::string::npos && Serial.output.find("WAIT")==std::string::npos);
}

const int8_t expectedEncoderPolarity[] = {+1, -1, +1, -1};
// Describe the Gray-code cycle independently of the firmware's transition table.
const uint8_t forwardGrayCycle[] = {0, 2, 3, 1};

unsigned grayCyclePosition(uint8_t state) {
  for(unsigned k=0;k<4;++k) if(forwardGrayCycle[k]==state) return k;
  assert(false);
  return 0;
}
int expectedRawEncoderStep(uint8_t before, uint8_t after) {
  const unsigned a=grayCyclePosition(before), b=grayCyclePosition(after);
  if(b==(a+1)%4) return +1;
  if(a==(b+1)%4) return -1;
  return 0; // unchanged state or opposite corners of the Gray-code cycle
}
void setEncoderPins(uint8_t index, uint8_t state) {
  pinInput[motors[index].encoderAPin]=state>>1;
  pinInput[motors[index].encoderBPin]=state&1;
}
void resetEncoderForTest(uint8_t index, uint8_t state=0) {
  setEncoderPins(index,state);
  previousEncoderState[index]=state;
  encoderCounts[index]=0;
  encoderAEdges[index]=0;
  encoderBEdges[index]=0;
  encoderInvalid[index]=0;
}
void checkedEncoderUpdate(uint8_t index) {
  const unsigned bank=motors[index].encoderAPin>=32 ? 1U : 0U;
  assert((motors[index].encoderBPin>=32 ? 1U : 0U)==bank);
  const unsigned selectedBefore=gpioReadCount[bank];
  const unsigned otherBefore=gpioReadCount[1-bank];
  const unsigned digitalBefore=digitalReadCalls;
  updateEncoder(index);
  assert(gpioReadCount[bank]==selectedBefore+1);
  assert(gpioReadCount[1-bank]==otherBefore);
  assert(digitalReadCalls==digitalBefore);
}
void testEncoderTransitions() {
  for(uint8_t index=0;index<MOTOR_COUNT;++index) {
    assert(motors[index].encoderPolarity==expectedEncoderPolarity[index]);
    for(uint8_t before=0;before<4;++before) {
      for(uint8_t after=0;after<4;++after) {
        // Unrelated high pins must not leak into either extracted A/B bit.
        for(auto& input: pinInput) input=1;
        resetEncoderForTest(index,before);
        encoderCounts[index]=17;
        encoderAEdges[index]=11;
        encoderBEdges[index]=13;
        encoderInvalid[index]=19;
        setEncoderPins(index,after);
        checkedEncoderUpdate(index);
        const uint8_t changed=before^after;
        assert(previousEncoderState[index]==after);
        assert(getEncoderCount(index)==17+
               expectedRawEncoderStep(before,after)*expectedEncoderPolarity[index]);
        assert(encoderAEdges[index]==11U+((changed&2)!=0));
        assert(encoderBEdges[index]==13U+((changed&1)!=0));
        assert(encoderInvalid[index]==19U+(changed==3));

        if(changed==3) {
          // An illegal jump records both edges but resynchronizes immediately:
          // the following valid forward edge counts from the observed state.
          const uint8_t next=forwardGrayCycle[(grayCyclePosition(after)+1)%4];
          setEncoderPins(index,next);
          checkedEncoderUpdate(index);
          const uint8_t resumedChange=after^next;
          assert(previousEncoderState[index]==next);
          assert(getEncoderCount(index)==17+expectedEncoderPolarity[index]);
          assert(encoderAEdges[index]==12U+((resumedChange&2)!=0));
          assert(encoderBEdges[index]==14U+((resumedChange&1)!=0));
          assert(encoderInvalid[index]==20);
        }
      }
    }
  }
}
void testEncoderCycles() {
  const uint8_t leftForward[] = {2,3,1,0};
  const uint8_t rightForward[] = {1,3,2,0};
  for(uint8_t index=0;index<MOTOR_COUNT;++index) {
    resetEncoderForTest(index);
    checkedEncoderUpdate(index); // repeated state must not invent an edge
    assert(getEncoderCount(index)==0 && previousEncoderState[index]==0);
    assert(encoderAEdges[index]==0 && encoderBEdges[index]==0 && encoderInvalid[index]==0);
    const bool right=index==FRONT_RIGHT || index==REAR_RIGHT;
    const auto* forward=right ? rightForward : leftForward;
    const auto* reverse=right ? leftForward : rightForward;
    int expectedCount=0;
    for(unsigned lap=0;lap<5;++lap) {
      const auto* cycle=lap<2 ? forward : reverse;
      const int direction=lap<2 ? +1 : -1;
      for(unsigned k=0;k<4;++k) {
        setEncoderPins(index,cycle[k]);
        checkedEncoderUpdate(index);
        expectedCount+=direction;
        assert(getEncoderCount(index)==expectedCount);
        assert(previousEncoderState[index]==cycle[k]);
        assert(encoderInvalid[index]==0);
      }
      assert(encoderAEdges[index]==2*(lap+1));
      assert(encoderBEdges[index]==2*(lap+1));
    }
    assert(getEncoderCount(index)==-4);
  }
}
int main() {
  // Startup must capture real nonzero pin states, with two wheels in each bank.
  const uint8_t startupStates[] = {1,2,3,1};
  for(uint8_t index=0;index<MOTOR_COUNT;++index) setEncoderPins(index,startupStates[index]);
  gpioReadCount[0]=gpioReadCount[1]=digitalReadCalls=0;
  // SPI startup must reset the sensor with its mode/WAKE pin held high.
  bno08x.needsHardwareReset=true;
  pinInput[IMU_INT_PIN]=LOW;
  bno08x.initializationCalls.clear();
  bno08x.hardwareResetHook=assertImuResetState;
  setup(); zero();
  assert(Serial.txBufferSizeAtBegin==2048);
  assert(imuAvailable && bno08x.initializationCalls=="BR");
  assert(bno08x.resetPin==25);
  assert(bno08x.spiCsPin==33 && bno08x.spiIntPin==26 && bno08x.spiBus==&SPI);
  assert(SPI.sck==22 && SPI.miso==21 && SPI.mosi==32 && SPI.ss==33);
  assert(pinDuty[IMU_CS_PIN]==HIGH && pinDuty[IMU_WAKE_PIN]==HIGH);
  assert(gpioReadCount[0]==2 && gpioReadCount[1]==2 && digitalReadCalls==1);
  for(uint8_t index=0;index<MOTOR_COUNT;++index) {
    assert(previousEncoderState[index]==startupStates[index]);
    assert(getEncoderCount(index)==0);
    assert(encoderAEdges[index]==0 && encoderBEdges[index]==0 && encoderInvalid[index]==0);
  }
  assert(!settings.headingEnabled && !fieldOrientedEnabled);
  for(auto v: settings.pwm) assert(v==177);
  assert(motors[FRONT_LEFT].dir1Pin==17 && motors[FRONT_RIGHT].dir1Pin==14);
  assert(motors[REAR_LEFT].dir1Pin==4 && motors[REAR_RIGHT].dir1Pin==27);
  assert(pinModes[34]==INPUT && pinModes[35]==INPUT && pinModes[36]==INPUT && pinModes[39]==INPUT);
  assert(IMU_SCK_PIN==22 && IMU_MISO_PIN==21 && IMU_MOSI_PIN==32);
  assert(IMU_CS_PIN==33 && IMU_INT_PIN==26 && IMU_RESET_PIN==25 && IMU_WAKE_PIN==16);
  assert(Serial.output.find("READY ESP32_MAKER_MECANUM_IMU_V1")!=std::string::npos);

  testEncoderTransitions();
  testEncoderCycles();
  for(auto& input: pinInput) input=0;
  for(uint8_t index=0;index<MOTOR_COUNT;++index) resetEncoderForTest(index);

  // Simulate the A/B order observed for physical forward on each wheel.
  const uint8_t positiveCycle[] = {2,3,1,0};
  const uint8_t negativeCycle[] = {1,3,2,0};
  for(uint8_t i=0;i<4;++i) {
    const auto* cycle = (i==FRONT_RIGHT || i==REAR_RIGHT) ? negativeCycle : positiveCycle;
    for(unsigned k=0;k<4;++k) {
      pinInput[motors[i].encoderAPin]=cycle[k]>>1;
      pinInput[motors[i].encoderBPin]=cycle[k]&1;
      checkedEncoderUpdate(i);
    }
    assert(getEncoderCount(i)==4);
    assert(encoderAEdges[i]==2 && encoderBEdges[i]==2 && encoderInvalid[i]==0);
  }

  cmd("V 1 0 0"); powerFor(250);
  for(uint8_t i=0;i<4;++i) {
    assert(outputController.wheels[i].applied==177);
    assert(pinDuty[motors[i].dir1Pin]==(i==REAR_RIGHT ? 0U:177U));
    assert(pinDuty[motors[i].dir2Pin]==(i==REAR_RIGHT ? 177U:0U));
  }
  Serial.output.clear();
  sendDiagnostics();
  assert(Serial.output.find("P FL PIN 17 DUTY 177 HZ 20000 PIN 12 DUTY 0 HZ 0")!=std::string::npos);
  assert(Serial.output.find("P RR PIN 27 DUTY 0 HZ 0 PIN 13 DUTY 177 HZ 20000")!=std::string::npos);
  // A peripheral mismatch must remain visible even if the software target is 177.
  pinDuty[17]=91;
  Serial.output.clear(); sendDiagnostics();
  assert(Serial.output.find("D FL PWM 177.0")!=std::string::npos);
  assert(Serial.output.find("P FL PIN 17 DUTY 91 HZ 20000")!=std::string::npos);
  pinDuty[17]=177;
  // A simulated blocking sensor call cannot retain stale motor power.
  timeHook=outputTick;
  bno08x.blockMs=400;
  pollImu(); zero(); assert(motorWatchdogTripped);
  timeHook=nullptr;
  cmd("X"); zero();

  for(const char* bad: {"V nan 0 0", "V 0 inf 0", "V 0 0 -inf", "V 1 0", "V 1 0 0 extra", "junk"}) {
    cmd("V 1 0 0"); powerFor(20); cmd(bad); zero(); assert(!outputController.live);
  }
  receive(std::string(128,'q') + "V 1 0 0\n");
  assert(!outputController.live); zero();
  receive("V 1 0 0\n"); assert(outputController.live);
  receive(std::string("q\0V 1 0 0\n",12)); assert(!outputController.live); zero();
  cmd("V 1 0 0"); powerFor(20);
  cmd("CFG SET pwm-fl 180"); zero(); assert(settings.pwm[0]==180);
  cmd("CFG SET heading-kp nan"); assert(std::isfinite(settings.headingKp));
  cmd("CFG RESET"); assert(settings.pwm[0]==177 && !settings.headingEnabled);

  // Stale orientation is not silently reused in field mode or I telemetry.
  imuAvailable=imuQuaternionValid=imuGyroValid=imuAccelerationValid=true;
  imuQx=imuQy=imuQz=0; imuQw=1; imuStatus=3;
  lastImuQuaternionMs=lastImuGyroMs=lastImuAccelerationMs=millis();
  cmd("F 1"); assert(fieldOrientedEnabled);
  cmd("V 1 0 0"); powerFor(20);
  fakeNow+=600;
  cmd("V 1 0 0"); zero(); assert(!outputController.live);
  lastImuEventMs=lastImuGyroMs=lastImuAccelerationMs=millis();
  Serial.output.clear(); sendImuTelemetry(); assert(Serial.output.find("STALE")!=std::string::npos);
  lastImuQuaternionMs=millis();
  cmd("F 1"); cmd("V 1 0 0"); powerFor(20);
  bno08x.resetPending=true; pollImu(); zero(); assert(!fieldOrientedEnabled);
  cmd("F 0"); cmd("V 1 0 0"); powerFor(20);
  cmd("IMU RETRY"); zero(); assert(!outputController.live);
  testImuHardwareResetRecovery();
  testStalledImuRecovery();
  testMissingReportAndMotionDeferral();
  testImuRecoveryRolloverAndDiagnostics();
  testQueuedImuArrivalFreshness();
  std::cout << "PASS: actual firmware mapping, defaults, coherent encoder startup/sampling, all transitions/polarities, resync/reverse counts, PWM polarity, blocked-sensor timeout, stop, malformed/overflow input, settings, stale IMU, SPI initialization, reset mode, failed-init invalidation, bounded missing/stale recovery, selective retries, fresh-event recovery, motion deferral, rollover, read-only diagnostics and queued arrival freshness\n";
}
