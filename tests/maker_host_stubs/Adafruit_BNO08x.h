#pragma once
#include "Wire.h"
enum { SH2_GAME_ROTATION_VECTOR=1, SH2_GYROSCOPE_CALIBRATED=2, SH2_LINEAR_ACCELERATION=3 };
struct sh2_SensorValue_t {
  unsigned sensorId=0; uint8_t status=3;
  struct {
    struct { float i=0,j=0,k=0,real=1; } gameRotationVector;
    struct { float x=0,y=0,z=0; } gyroscope, linearAcceleration;
  } un;
};
struct Adafruit_BNO08x {
  bool resetPending=false;
  bool eventPending=false;
  unsigned blockMs=0;
  sh2_SensorValue_t nextEvent;
  explicit Adafruit_BNO08x(int) {}
  bool begin_I2C(unsigned,TwoWire*) { return true; }
  bool enableReport(unsigned,unsigned) { return true; }
  bool wasReset() { bool v=resetPending; resetPending=false; return v; }
  bool getSensorEvent(sh2_SensorValue_t* e) {
    if(blockMs) { auto ms=blockMs; blockMs=0; delay(ms); }
    if(!eventPending) return false;
    *e=nextEvent; eventPending=false; return true;
  }
};
