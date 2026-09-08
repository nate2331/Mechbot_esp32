#pragma once
#include "Wire.h"
#include "SPI.h"
#define SH2_HAL_MAX_TRANSFER_OUT 256
#define SH2_HAL_MAX_TRANSFER_IN 384

struct sh2_SensorEvent_t;
inline void (*fakeSh2SensorCallback)(void*,sh2_SensorEvent_t*)=nullptr;
inline void* fakeSh2SensorCookie=nullptr;
inline void (*fakeSh2ServiceHook)()=nullptr;
struct Adafruit_BNO08x;
inline Adafruit_BNO08x* fakeSh2ActiveSensor=nullptr;
inline void sh2_service();

struct sh2_Hal_t {
  int (*open)(sh2_Hal_t*)=nullptr;
  void (*close)(sh2_Hal_t*)=nullptr;
  int (*read)(sh2_Hal_t*,uint8_t*,unsigned,uint32_t*)=nullptr;
  int (*write)(sh2_Hal_t*,uint8_t*,unsigned)=nullptr;
  uint32_t (*getTimeUs)(sh2_Hal_t*)=nullptr;
};
// The installed SH-2/SHTP library has one slot. HAL open failure does not
// release it, and sh2_close has no null-session guard of its own.
inline sh2_Hal_t* fakeShtpSlot=nullptr;
inline sh2_Hal_t* fakeSh2Session=nullptr;
inline unsigned fakeSh2Allocations=0, fakeSh2CloseCalls=0, fakeSh2NullCloseCalls=0;
inline unsigned fakeBeginWithLiveSession=0;
inline void sh2_close() {
  if(!fakeSh2Session) { ++fakeSh2NullCloseCalls; return; }
  ++fakeSh2CloseCalls;
  if(fakeSh2Session->close) fakeSh2Session->close(fakeSh2Session);
  fakeShtpSlot=nullptr;
  fakeSh2Session=nullptr;
  fakeSh2SensorCallback=nullptr;
  fakeSh2SensorCookie=nullptr;
}
enum { SH2_GAME_ROTATION_VECTOR=1, SH2_GYROSCOPE_CALIBRATED=2, SH2_LINEAR_ACCELERATION=3 };
struct sh2_SensorValue_t {
  unsigned sensorId=0; uint8_t status=3;
  struct {
    struct { float i=0,j=0,k=0,real=1; } gameRotationVector;
    struct { float x=0,y=0,z=0; } gyroscope, linearAcceleration;
  } un;
};
struct sh2_SensorEvent_t {
  sh2_SensorValue_t decoded;
};
inline int sh2_setSensorCallback(void (*callback)(void*,sh2_SensorEvent_t*),void* cookie) {
  fakeSh2SensorCallback=callback;
  fakeSh2SensorCookie=cookie;
  return 0;
}
inline int sh2_decodeSensorEvent(sh2_SensorValue_t* value,sh2_SensorEvent_t* event) {
  *value=event->decoded;
  return 0;
}
struct Adafruit_BNO08x {
  bool resetPending=false;
  bool eventPending=false;
  bool beginSucceeds=true;
  bool needsHardwareReset=false;
  unsigned hardwareResetCalls=0;
  unsigned beginCalls=0;
  unsigned nativeReadCalls=0, nativeWriteCalls=0, nativeOpenCalls=0;
  unsigned nativeReadDelayMs=0;
  int nativeReadResult=0, nativeWriteResult=-1;
  unsigned spiCsPin=0, spiIntPin=0;
  SPIClass* spiBus=nullptr;
  void (*nativeReadHook)()=nullptr;
  void (*nativeWriteHook)()=nullptr;
  void (*afterOpenHook)(sh2_Hal_t*)=nullptr;
  int resetPin;
  std::string initializationCalls;
  void (*hardwareResetHook)()=nullptr;
  unsigned blockMs=0;
  unsigned enableReportCalls[4]={};
  void (*enableReportHook)()=nullptr;
  sh2_SensorValue_t nextEvent;
  explicit Adafruit_BNO08x(int pin): resetPin(pin) {}
  void hardwareReset() {
    ++hardwareResetCalls;
    initializationCalls+='R';
    if(hardwareResetHook) hardwareResetHook();
    if(resetPin>=0) needsHardwareReset=false;
  }
  bool begin_I2C(unsigned,TwoWire*) {
    ++beginCalls;
    initializationCalls+='B';
    // Model the address probe before the library's internal hardware reset.
    // An explicit pre-probe reset is needed to recover this simulated NACK.
    return beginSucceeds && !needsHardwareReset;
  }
  bool begin_SPI(uint8_t csPin,uint8_t intPin,SPIClass* spi,int32_t sensorId=0) {
    if(fakeShtpSlot) ++fakeBeginWithLiveSession;
    ++beginCalls;
    initializationCalls+='B';
    spiCsPin=csPin; spiIntPin=intPin; spiBus=spi;
    pinMode(intPin,INPUT_PULLUP);
    active=this;
    fakeSh2ActiveSensor=this;
    _HAL.open=nativeOpen;
    _HAL.close=nativeClose;
    _HAL.read=nativeRead;
    _HAL.write=nativeWrite;
    return _init(sensorId);
  }
  sh2_Hal_t* testHal() { return &_HAL; }
  bool enableReport(unsigned id,unsigned) {
    if(id<4) ++enableReportCalls[id];
    if(enableReportHook) enableReportHook();
    return true;
  }
  bool wasReset() { bool v=resetPending; resetPending=false; return v; }
  bool getSensorEvent(sh2_SensorValue_t* e) {
    if(blockMs) { auto ms=blockMs; blockMs=0; delay(ms); }
    if(!eventPending) return false;
    *e=nextEvent; eventPending=false; return true;
  }
 protected:
  sh2_Hal_t _HAL;
  virtual bool _init(int32_t) {
    hardwareReset();
    fakeSh2Session=nullptr; // sh2_open clears its global state before allocation.
    if(fakeShtpSlot) return false;
    fakeShtpSlot=&_HAL;
    fakeSh2Session=&_HAL;
    ++fakeSh2Allocations;
    if(_HAL.open) _HAL.open(&_HAL); // real shtp_open ignores this result.
    if(afterOpenHook) afterOpenHook(&_HAL);
    // A product-ID failure also leaves an allocated session for caller cleanup.
    return beginSucceeds && !needsHardwareReset;
  }
 private:
  inline static Adafruit_BNO08x* active=nullptr;
  static int nativeOpen(sh2_Hal_t*) { ++active->nativeOpenCalls; return 0; }
  static void nativeClose(sh2_Hal_t*) {}
  static int nativeRead(sh2_Hal_t*,uint8_t*,unsigned,uint32_t*) {
    ++active->nativeReadCalls;
    if(active->nativeReadHook) active->nativeReadHook();
    if(active->nativeReadDelayMs) delay(active->nativeReadDelayMs);
    return active->nativeReadResult;
  }
  static int nativeWrite(sh2_Hal_t*,uint8_t*,unsigned len) {
    ++active->nativeWriteCalls;
    if(active->nativeWriteHook) active->nativeWriteHook();
    return active->nativeWriteResult<0 ? static_cast<int>(len) : active->nativeWriteResult;
  }
};
inline void sh2_service() {
  if(fakeSh2ServiceHook) fakeSh2ServiceHook();
  if(!fakeSh2ActiveSensor) return;
  if(fakeSh2ActiveSensor->blockMs) {
    const auto ms=fakeSh2ActiveSensor->blockMs;
    fakeSh2ActiveSensor->blockMs=0;
    delay(ms);
  }
  if(fakeSh2ActiveSensor->eventPending && fakeSh2SensorCallback) {
    sh2_SensorEvent_t event;
    event.decoded=fakeSh2ActiveSensor->nextEvent;
    fakeSh2ActiveSensor->eventPending=false;
    fakeSh2SensorCallback(fakeSh2SensorCookie,&event);
  }
}
