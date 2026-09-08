#pragma once
#include "Arduino.h"
#include <vector>
#define MSBFIRST 1
#define SPI_MODE3 3
struct SPISettings {
  uint32_t clock;
  uint8_t order, mode;
  SPISettings(uint32_t hz,uint8_t bitOrder,uint8_t dataMode)
      : clock(hz),order(bitOrder),mode(dataMode) {}
};
inline void (*spiTraceHook)(const char*)=nullptr;
inline void (*spiTransferHook)(const uint8_t*,uint8_t*,uint32_t)=nullptr;
struct SPIClass {
  int sck=-1, miso=-1, mosi=-1, ss=-1;
  unsigned beginCalls=0;
  unsigned writeCalls=0;
  unsigned transferCalls=0;
  bool transactionActive=false;
  bool beginSucceeds=true;
  SPISettings lastSettings{0,0,0};
  std::vector<uint8_t> lastWritten;
  bool begin(int sckPin=-1, int misoPin=-1, int mosiPin=-1, int ssPin=-1) {
    sck=sckPin; miso=misoPin; mosi=mosiPin; ss=ssPin; ++beginCalls;
    return beginSucceeds;
  }
  void end() {}
  void beginTransaction(const SPISettings& settings) {
    lastSettings=settings; transactionActive=true;
    if(spiTraceHook) spiTraceHook("begin");
  }
  void writeBytes(const uint8_t* data,uint32_t len) {
    ++writeCalls;
    lastWritten.assign(data,data+len);
    if(spiTraceHook) spiTraceHook("write");
  }
  void endTransaction() {
    transactionActive=false;
    if(spiTraceHook) spiTraceHook("end");
  }
  void transferBytes(const uint8_t* data,uint8_t* output,uint32_t len) {
    ++transferCalls;
    lastWritten.assign(data,data+len);
    if(spiTraceHook) spiTraceHook("transfer");
    if(spiTransferHook) spiTransferHook(data,output,len);
    else std::memset(output,0,len);
  }
};
inline SPIClass SPI;
