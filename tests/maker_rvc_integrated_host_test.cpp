#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#define MAKER_IMU_RVC 1
#include "../Maker_Mechbot/Maker_Mechbot.ino"

void command(const char* text) {
  std::vector<char> line(text,text+std::strlen(text)+1); processCommand(line.data());
}
std::array<uint8_t,19> frame(uint8_t index,int16_t yaw=0) {
  std::array<uint8_t,19> bytes={0xAA,0xAA,index};
  const int16_t values[]={yaw,-75,40,-8,-12,974};
  for(unsigned i=0;i<6;++i) { const uint16_t raw=static_cast<uint16_t>(values[i]); bytes[3+i*2]=raw; bytes[4+i*2]=raw>>8; }
  for(unsigned i=2;i<18;++i) bytes[18]+=bytes[i];
  return bytes;
}
void push(const std::array<uint8_t,19>& bytes) { for(auto byte:bytes) Serial1.input.push_back(byte); }
void sample(uint8_t index,int16_t angle=0) { fakeNow+=10; push(frame(index,angle)); pollImu(); }
void qualify(uint8_t start=0) { for(unsigned i=0;i<5;++i) sample(start+i); }
void stopped() {
  assert(!commandActive && !motionRequested && !outputController.live);
  for(unsigned i=0;i<4;++i) assert(pendingMotorCommands[i]==0 && pinDuty[motors[i].dir1Pin]==0 && pinDuty[motors[i].dir2Pin]==0);
}
void near(float a,float b) { assert(std::fabs(a-b)<.001F); }

int main(int argc,char** argv) {
  if(argc>1) {
    const std::string failure=argv[1];
    if(failure=="uart") fakeUartBeginOk=false;
    else if(failure=="rx") fakeUartWrongRx=true;
    else return 2;
    setup(); stopped(); assert(!imuAvailable);
    command("IMU ACCEPT"); command("F 1"); assert(!fieldOrientedEnabled);
    std::cout << "PASS: integrated RVC " << failure << " initialization failure stays unavailable/stopped\n";
    return 0;
  }
  setup(); stopped();
  assert(Serial1.rx==21 && Serial1.tx==-1 && Serial1.baud==115200 && Serial1.bufferSize==8192);
  for(unsigned pin:{16U,22U,25U,26U,32U,33U}) assert(pinModes[pin]==0 && pinDuty[pin]==0);
  assert(Serial.output.find("READY ESP32_MAKER_MECANUM_RVC_V1")!=std::string::npos);
  assert(Serial.output.find("Maker mapping:")==std::string::npos);
  assert(!settings.headingEnabled && !fieldOrientedEnabled);
  command("IMU ACCEPT"); assert(!rvc.accepted(millis()));
  for(unsigned i=0;i<4;++i) sample(i);
  assert(!rvc.fresh(millis())); sample(4); assert(rvc.fresh(millis()));
  command("F 1"); assert(!fieldOrientedEnabled); stopped();
  command("IMU ACCEPT"); assert(rvc.accepted(millis())); stopped();
  command("F 1"); assert(fieldOrientedEnabled); stopped();
  sample(5,9000); command("V 0.5 0 0");
  near(pendingMotorCommands[0],88.5F); near(pendingMotorCommands[1],-88.5F);
  Serial.output.clear(); sendImuTelemetry();
  assert(Serial.output.find(" READY 90.00 -0.75 0.40 -8 -12 974 1 0 0 0")!=std::string::npos);
  assert(!imuQuaternionValid && !imuGyroValid && !imuAccelerationValid); // no invented fields

  auto bad=frame(6); bad[18]^=1; push(bad); pollImu();
  stopped(); assert(fieldFaultLatched && !rvc.accepted(millis()) && rvc.badChecksums()==1);
  qualify(7); command("V 0.5 0 0"); stopped();
  command("IMU ACCEPT"); command("V 0.5 0 0"); stopped(); // mode acknowledgement is still required
  command("F 1"); command("V 0.5 0 0"); assert(commandActive);
  const auto gapsBefore=rvc.discontinuities();
  sample(50); stopped(); assert(rvc.discontinuities()==gapsBefore+1 && !rvc.accepted(millis()));
  qualify(51); command("IMU ACCEPT"); command("F 1"); command("V 0.5 0 0");
  Serial1.errorCallback(UART_FRAME_ERROR);
  float heading; assert(!readCurrentYaw(heading)); pollImu(); stopped();
  assert(rvc.uartErrors()==1);

  qualify(60); command("IMU ACCEPT"); command("F 1"); command("V 0.5 0 0");
  for(unsigned i=0;i<100;++i) push(frame(65+i));
  const auto queued=Serial1.input.size();
  pollImu(); stopped(); assert(Serial1.input.size()==queued-1024);
  pollImu(); assert(Serial1.input.empty() && rvc.age(millis())==-1 && !rvc.fresh(millis()));
  qualify(170); command("IMU ACCEPT"); command("F 1");
  fakeNow+=101; push(frame(175)); pollImu(); stopped(); assert(!rvc.accepted(millis()));
  qualify(180); command("IMU ACCEPT"); command("F 1");
  for(unsigned i=0;i<6;++i) { fakeNow+=90; pollImu(); }
  assert(!rvc.accepted(millis()) && fieldFaultLatched); stopped();

  command("F 0"); command("V 0.5 0 0"); assert(commandActive); // manual robot mode remains available
  fakeNow+=301; assert(outputController.tick(millis()));
  for(auto& wheel:outputController.wheels) assert(wheel.applied==0);
  command("X");
  fakeNow=UINT32_MAX-40; initializeImu(); qualify(0);
  assert(rvc.fresh(millis())); command("IMU ACCEPT"); assert(rvc.accepted(millis()));
  command("IMU REVOKE"); assert(!rvc.accepted(millis())); stopped();
  command("IMU ACCEPT"); command("IMU RETRY"); assert(!rvc.accepted(millis())); stopped();
  std::cout << "PASS: integrated receive-only RVC, qualification/acceptance, raw telemetry, field transform, checksum/index/UART faults, bounded backlog drain, loop gap/stale, rollover, revoke/retry and motor watchdog\n";
}
