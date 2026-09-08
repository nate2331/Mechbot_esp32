#include <cassert>
#include <iostream>
#include "../Maker_IMU_Bus_Diagnostic/Maker_IMU_Bus_Diagnostic.ino"
unsigned resets = 0;
void pinWrite(unsigned p,unsigned value) {
  if (p==RST && value==LOW) { assert(pinModes[p]==OUTPUT); ++resets; }
  if (p==P0) assert(pinModes[p]==OUTPUT);
  for (uint8_t motor:MOTOR_PINS) if(p==motor) assert(value==LOW);
}
int main(int argc,char**) {
  Wire.ack=argc>1;
  digitalWriteHook=pinWrite;
  pinInput[INT_PIN]=HIGH;
  setup();
  assert(resets==5 && Wire.beginCalls==5 && Wire.endCalls==5);
  assert(!activeUart && !wireStarted && pinDuty[P0]==LOW && pinModes[RST]==INPUT);
  for(uint8_t p:MOTOR_PINS) assert(pinModes[p]==OUTPUT && pinDuty[p]==LOW && !pinAttached[p]);
  assert(millis() < 190000);
  for(unsigned i=0;i<5;++i) {
    if(Wire.ack) {
      assert(results[i].foundAddress==0x4A && results[i].inputs>50);
      assert(results[i].featureSeen && results[i].interval==results[i].requestedInterval);
      assert(results[i].samples>50 && results[i].lastSampleMs-results[i].firstSampleMs>=30000);
    } else assert(results[i].foundAddress==0 && results[i].inputs==0);
  }
  for(unsigned i=0;i<120;++i) {fakeNow+=1000;loop();}
  assert(resets==5 && Wire.beginCalls==5 && !activeUart);
  assert(Serial.output.find("BUS DIAGNOSTIC SUMMARY END")!=std::string::npos);
  std::cout << "PASS: automatic bus diagnostic " << (Wire.ack ? "streaming simulator" : "absent device")
            << "; motors idle; finite stages; valid feature readback; no idle reset loop\n";
}
