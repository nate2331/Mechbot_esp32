// Actual Maker sketch with fake IO. No serial device or motor hardware is opened.
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include "../Maker_Mechbot/Maker_Mechbot.ino"

void command(const char* text) {
  std::vector<char> line(text, text + std::strlen(text) + 1);
  processCommand(line.data());
}
void yaw(float radians, uint8_t status=3) {
  imuAvailable=imuQuaternionValid=imuGyroValid=imuAccelerationValid=true;
  imuQx=imuQy=0;
  imuQz=std::sin(radians/2); imuQw=std::cos(radians/2);
  imuStatus=status;
  lastImuEventMs=lastImuQuaternionMs=lastImuGyroMs=lastImuAccelerationMs=millis();
}
void near(float a,float b) { assert(std::fabs(a-b)<0.0001F); }
void stopped() {
  assert(!commandActive && !motionRequested && !outputController.live);
  for(unsigned i=0;i<4;++i) {
    assert(pendingMotorCommands[i]==0);
    assert(pinDuty[motors[i].dir1Pin]==0 && pinDuty[motors[i].dir2Pin]==0);
  }
}
void wheelTargets(float fl,float fr,float rl,float rr) {
  const float values[]={fl,fr,rl,rr};
  for(unsigned i=0;i<4;++i) near(pendingMotorCommands[i]/177.0F,values[i]);
}

int main() {
  pinInput[IMU_INT_PIN]=LOW;
  setup(); stopped();
  assert(!settings.headingEnabled && !fieldOrientedEnabled && !fieldFaultLatched);

  command("CFG SET heading-enabled 1");
  yaw(0); command("V 0.5 0 0");
  near(headingTargetYaw,0); near(lastHeadingCorrection,0);
  yaw(.1F); command("V 0.5 0 0");
  near(lastHeadingError,-.1F); near(lastHeadingCorrection,-.07F);
  wheelTargets(.57F,.43F,.57F,.43F);
  yaw(.01F); command("V 0.5 0 0"); near(lastHeadingCorrection,0); // deadband
  yaw(1); command("V 0.5 0 0"); near(lastHeadingCorrection,-.3F); // saturation
  yaw(.25F); command("V 0.5 0 0.2");
  near(headingTargetYaw,.25F); near(lastHeadingCorrection,0); // deliberate turn wins
  yaw(.3F); command("V 0.5 0 0"); near(lastHeadingCorrection,-.035F);
  command("V 0 0 0"); assert(!headingTargetValid); wheelTargets(0,0,0,0);
  yaw(-1); command("V 0.5 0 0"); near(headingTargetYaw,-1); near(lastHeadingCorrection,0);

  const float degree=NavigationMath::PI_F/180;
  command("X"); yaw(179*degree); command("V 0.5 0 0");
  yaw(-179*degree); command("V 0.5 0 0");
  near(lastHeadingError,-2*degree); near(lastHeadingCorrection,-1.4F*degree);
  command("CFG SET heading-sign -1");
  yaw(0); command("V 0.5 0 0"); yaw(.1F); command("V 0.5 0 0");
  near(lastHeadingCorrection,.07F);
  // Current robot-relative behavior explicitly bypasses missing heading.
  fakeNow+=501; command("V 0.5 0 0");
  assert(!headingTargetValid); near(lastHeadingCorrection,0); wheelTargets(.5F,.5F,.5F,.5F);
  command("CFG RESET");

  yaw(0,0); command("F 1"); assert(!fieldOrientedEnabled); stopped();
  yaw(0); command("F 1"); assert(fieldOrientedEnabled && fieldReferenceValid); stopped();
  command("V 0.5 0 0"); wheelTargets(.5F,.5F,.5F,.5F);
  yaw(90*degree); command("V 0.5 0 0"); wheelTargets(.5F,-.5F,-.5F,.5F);
  yaw(-90*degree); command("V 0.5 0 0"); wheelTargets(-.5F,.5F,.5F,-.5F);
  yaw(180*degree); command("V 0.5 0 0"); wheelTargets(-.5F,-.5F,-.5F,-.5F);
  command("Z"); stopped(); command("V 0.5 0 0"); wheelTargets(.5F,.5F,.5F,.5F);

  // Fresh reports alone cannot resume a command after field heading is lost.
  fakeNow+=501; command("V 0.5 0 0"); stopped(); assert(fieldFaultLatched);
  command("X"); yaw(0); command("V 0.5 0 0"); stopped();
  command("Z"); command("V 0.5 0 0"); stopped(); // zero is not a mode choice
  imuQuaternionValid=false; command("F 1"); assert(fieldFaultLatched); stopped();
  yaw(0); command("F 1"); assert(!fieldFaultLatched); stopped();
  command("V 0.5 0 0"); wheelTargets(.5F,.5F,.5F,.5F);

  // Regression: sensor reset used to silently reinterpret subsequent V as robot-relative.
  bno08x.resetPending=true; pollImu(); stopped();
  assert(!fieldOrientedEnabled && fieldFaultLatched);
  yaw(0); command("V 0.5 0 0"); stopped();
  command("CFG RESET"); command("V 0.5 0 0"); stopped();
  Serial.output.clear(); sendNavigationTelemetry();
  assert(Serial.output.find(" 0 0 0\n")!=std::string::npos); // hold/field/ready
  command("F 0"); assert(!fieldFaultLatched); stopped();
  command("V 0.5 0 0"); wheelTargets(.5F,.5F,.5F,.5F);
  command("X"); stopped();
  std::cout << "PASS: actual Maker heading target, correction sign/limit/deadband, manual turn, idle, wrap, field cardinal headings, zero, stale/reset latch and explicit mode recovery\n";
}
