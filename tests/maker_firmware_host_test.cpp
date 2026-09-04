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
int main() {
  setup(); zero();
  assert(!settings.headingEnabled && !fieldOrientedEnabled);
  for(auto v: settings.pwm) assert(v==177);
  assert(motors[FRONT_LEFT].dir1Pin==17 && motors[FRONT_RIGHT].dir1Pin==14);
  assert(motors[REAR_LEFT].dir1Pin==4 && motors[REAR_RIGHT].dir1Pin==27);
  assert(pinModes[34]==INPUT && pinModes[35]==INPUT && pinModes[36]==INPUT && pinModes[39]==INPUT);
  assert(IMU_SDA_PIN==21 && IMU_SCL_PIN==22 && IMU_RESET_PIN==-1);
  assert(Serial.output.find("READY ESP32_MAKER_MECANUM_IMU_V1")!=std::string::npos);

  // Simulate the A/B order observed for physical forward on each wheel.
  const uint8_t positiveCycle[] = {2,3,1,0};
  const uint8_t negativeCycle[] = {1,3,2,0};
  for(uint8_t i=0;i<4;++i) {
    const auto* cycle = (i==FRONT_RIGHT || i==REAR_RIGHT) ? negativeCycle : positiveCycle;
    for(unsigned k=0;k<4;++k) {
      pinInput[motors[i].encoderAPin]=cycle[k]>>1;
      pinInput[motors[i].encoderBPin]=cycle[k]&1;
      updateEncoder(i);
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
  // A simulated blocking I2C call cannot retain stale motor power.
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
  std::cout << "PASS: actual firmware mapping, defaults, PWM polarity, blocked-I2C timeout, stop, malformed/overflow input, settings, stale IMU and reset\n";
}
