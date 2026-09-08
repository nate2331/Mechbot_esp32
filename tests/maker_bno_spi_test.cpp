// Exercise the real adapter against an observable full-duplex sensor model.
#include <cassert>
#include <iostream>
#include <vector>
#include "../Maker_Mechbot/MakerBnoSpi.h"

constexpr unsigned RESET=25, WAKE=16, INT=26, CS=33;
std::vector<std::string> events;
std::vector<uint8_t> incoming, transmitted;
std::vector<unsigned> transferSizes;
unsigned cursor=0, serviceCalls=0;
uint32_t readyAt=0;
bool waitingForStartup=false;

void resetCheck() {
  assert(pinModes[WAKE]==OUTPUT && pinDuty[WAKE]==HIGH);
  events.push_back("reset");
}
void onTime() {
  if(waitingForStartup) assert(pinDuty[WAKE]==HIGH);
  if(static_cast<int32_t>(millis()-readyAt)>=0) pinInput[INT]=LOW;
}
void onPin(unsigned pin,unsigned value) {
  if(pin==WAKE) events.push_back(value==LOW ? "wake-low" : "wake-high");
  if(pin==CS) {
    events.push_back(value==LOW ? "cs-low" : "cs-high");
    if(value==LOW) {
      assert(SPI.transactionActive);
      cursor=0;
      transmitted.clear();
      transferSizes.clear();
      // The BNO085 is permitted to release INT immediately when selected.
      pinInput[INT]=HIGH;
    }
  }
}
void onSpi(const char* action) {
  events.push_back(action);
  if(std::string(action)=="transfer") {
    assert(pinDuty[CS]==LOW && SPI.transactionActive && pinInput[INT]==HIGH);
    assert(SPI.lastSettings.clock==1000000);
    assert(SPI.lastSettings.order==MSBFIRST && SPI.lastSettings.mode==SPI_MODE3);
  }
  if(std::string(action)=="end") assert(pinDuty[CS]==HIGH);
}
void onTransfer(const uint8_t* tx,uint8_t* rx,uint32_t len) {
  assert(pinDuty[CS]==LOW && SPI.transactionActive);
  if(cursor==0) assert(len==4);
  transferSizes.push_back(len);
  for(unsigned i=0;i<len;++i) {
    transmitted.push_back(tx[i]);
    rx[i]=cursor<incoming.size() ? incoming[cursor] : 0xFF;
    ++cursor;
  }
}
std::vector<uint8_t> packet(unsigned len,bool continuation=false,uint8_t seq=9) {
  assert(len>=4 && len<=SH2_HAL_MAX_TRANSFER_IN);
  std::vector<uint8_t> result(len);
  for(unsigned i=4;i<len;++i) result[i]=static_cast<uint8_t>(i*7U+seq);
  result[0]=static_cast<uint8_t>(len);
  result[1]=static_cast<uint8_t>((len>>8)|(continuation?0x80:0));
  result[2]=3;
  result[3]=seq;
  return result;
}
void prepare(const std::vector<uint8_t>& rx) {
  incoming=rx;
  pinInput[INT]=LOW;
  events.clear();
}
void checkZeroTx(unsigned len) {
  assert(transmitted==std::vector<uint8_t>(len,0));
}
void probeAfterFailedOpen(sh2_Hal_t* hal) {
  // Real SHTP ignores HAL.open failure; gated write must fail, never return its
  // unbounded-retry sentinel (zero), and never pull WAKE low before SPI boot.
  assert(pinInput[INT]==HIGH && pinDuty[WAKE]==HIGH);
  uint8_t bytes[5]={5,0,1,0,1};
  uint32_t stamp=0;
  const uint32_t started=micros();
  const unsigned calls=SPI.transferCalls;
  assert(hal->read(hal,bytes,sizeof(bytes),&stamp)==0);
  assert(hal->write(hal,bytes,sizeof(bytes))<0);
  assert(micros()==started && SPI.transferCalls==calls && pinDuty[WAKE]==HIGH);
}
void emitEvent(unsigned id,float marker=0) {
  assert(fakeSh2SensorCallback && fakeSh2SensorCookie);
  sh2_SensorEvent_t event;
  event.decoded.sensorId=id;
  event.decoded.un.gyroscope.x=marker;
  fakeSh2SensorCallback(fakeSh2SensorCookie,&event);
}
void bundledReports() {
  ++serviceCalls;
  fakeSh2ServiceHook=nullptr;
  emitEvent(SH2_GAME_ROTATION_VECTOR);
  emitEvent(SH2_GYROSCOPE_CALIBRATED);
  emitEvent(SH2_LINEAR_ACCELERATION);
}

int main() {
  MakerBnoSpi sensor(RESET,WAKE,INT);
  sensor.hardwareResetHook=resetCheck;
  digitalWriteHook=onPin;
  spiTraceHook=onSpi;
  spiTransferHook=onTransfer;
  assert(SPI.begin(22,21,32,CS));
  digitalWrite(CS,HIGH);
  assert(!sensor.begin_SPI(CS,27,&SPI));
  assert(!sensor.begin_SPI(CS,INT,nullptr));
  assert(sensor.beginCalls==0 && sensor.hardwareResetCalls==0);
  assert(fakeSh2NullCloseCalls==0 && fakeSh2CloseCalls==0);

  pinInput[INT]=HIGH;
  sensor.afterOpenHook=probeAfterFailedOpen;
  assert(!sensor.begin_SPI(CS,INT,&SPI));
  sensor.afterOpenHook=nullptr;
  assert(fakeNow==500 && sensor.hardwareResetCalls==1);
  assert(fakeSh2Allocations==1 && fakeSh2CloseCalls==1);
  assert(!fakeShtpSlot && !fakeSh2Session && fakeSh2NullCloseCalls==0);
  assert(sensor.nativeReadCalls==0 && SPI.transferCalls==0);

  pinInput[INT]=HIGH;
  const uint32_t startupStart=millis();
  readyAt=startupStart+40;
  waitingForStartup=true;
  timeHook=onTime;
  assert(sensor.begin_SPI(CS,INT,&SPI));
  assert(millis()-startupStart==40 && sensor.hardwareResetCalls==2);
  assert(fakeSh2Allocations==2 && fakeSh2CloseCalls==1);
  assert(sensor.nativeOpenCalls==0 && pinDuty[WAKE]==HIGH);
  waitingForStartup=false;
  timeHook=nullptr;

  sh2_Hal_t* hal=sensor.testHal();
  uint8_t buffer[SH2_HAL_MAX_TRANSFER_IN]={};
  uint32_t stamp=0;
  pinInput[INT]=HIGH;
  const uint32_t idleStart=micros();
  for(unsigned i=0;i<100;++i) assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==0);
  assert(micros()==idleStart && sensor.nativeReadCalls==0);
  assert(hal->getTimeUs(hal)==micros());
  assert(sensor.hardwareResetCalls==2);

  // Header and payload share one CS assertion even when INT goes high on CS.
  const auto first=packet(19);
  prepare(first);
  const uint32_t firstStamp=micros();
  assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==19);
  assert(stamp==firstStamp && std::vector<uint8_t>(buffer,buffer+19)==first);
  assert(events==std::vector<std::string>({"wake-high","begin","cs-low","transfer","transfer","cs-high","end"}));
  assert(transferSizes==std::vector<unsigned>({4,15}));
  checkZeroTx(19);
  assert(pinInput[INT]==HIGH && !SPI.transactionActive);
  assert(sensor.nativeReadCalls==0 && sensor.nativeWriteCalls==0);

  // Zero-length is a null transfer, not an error or a reason to wait/reset.
  const uint32_t beforeNull=micros();
  prepare({0,0,0,0});
  assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==0);
  assert(transferSizes==std::vector<unsigned>({4}));
  assert(micros()==beforeNull && sensor.hardwareResetCalls==2);
  assert(sensor.diagnostics().nullHeaders==1);
  // Four bytes, a full-size packet and the continuation bit are preserved.
  for(const auto& rx: {packet(4),packet(SH2_HAL_MAX_TRANSFER_IN,true)}) {
    prepare(rx);
    assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==static_cast<int>(rx.size()));
    assert(std::vector<uint8_t>(buffer,buffer+rx.size())==rx);
    checkZeroTx(static_cast<unsigned>(rx.size()));
  }
  // Bad lengths cannot drive out-of-bounds reads or an unbounded clock stream.
  for(unsigned malformed: {1U,2U,3U,385U,0xFFFFU}) {
    prepare({static_cast<uint8_t>(malformed),static_cast<uint8_t>(malformed>>8),0,0});
    assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==0);
    assert(transferSizes==std::vector<unsigned>({4}));
    assert(pinDuty[CS]==HIGH && !SPI.transactionActive);
  }
  assert(sensor.diagnostics().badHeaders==5);
  prepare(packet(12));
  uint8_t small[8]={0,0,0,0,0xA5,0xA5,0xA5,0xA5};
  assert(hal->read(hal,small,4,&stamp)==0);
  for(unsigned i=4;i<8;++i) assert(small[i]==0xA5);
  assert(sensor.diagnostics().rxBufferTooSmall==1 && sensor.queuedPackets()==0);
  assert(hal->read(hal,nullptr,sizeof(buffer),&stamp)==0);
  assert(hal->read(hal,buffer,3,&stamp)==0);

  // Full-duplex writes retain RX both when RX is longer and when TX is longer.
  std::vector<uint8_t> tx=packet(9);
  for(const auto& rx: {packet(18),packet(6)}) {
    prepare(rx);
    const auto original=tx;
    assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==9);
    assert(events==std::vector<std::string>({"wake-low","begin","cs-low","transfer","transfer","cs-high","end","wake-high"}));
    std::vector<uint8_t> expectedTx=tx;
    expectedTx.resize(std::max(tx.size(),rx.size()),0);
    assert(transmitted==expectedTx && tx==original);
    assert(sensor.queuedPackets()==1);
    const uint32_t rxStamp=micros();
    delay(8);
    const unsigned calls=SPI.transferCalls;
    assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==static_cast<int>(rx.size()));
    assert(stamp==rxStamp && std::vector<uint8_t>(buffer,buffer+rx.size())==rx);
    assert(SPI.transferCalls==calls && sensor.queuedPackets()==0);
  }
  prepare({0xFF,0xFF,0xFF,0xFF});
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==9);
  assert(transmitted==tx && transferSizes==std::vector<unsigned>({4,5}));
  assert(sensor.queuedPackets()==0 && sensor.diagnostics().badHeaders==6);
  prepare({0,0,0,0});
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==9);
  assert(transmitted==tx && sensor.queuedPackets()==0);

  // A full RX queue returns backpressure BEFORE WAKE or SPI, allowing SHTP's
  // retry loop to read a packet, then retry without discarding a duplex response.
  for(unsigned i=0;i<4;++i) {
    prepare(packet(6,false,static_cast<uint8_t>(i)));
    assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==9);
  }
  assert(sensor.queuedPackets()==4);
  events.clear();
  const uint32_t fullAt=micros();
  const unsigned fullCalls=SPI.transferCalls;
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==0);
  assert(events.empty() && micros()==fullAt && SPI.transferCalls==fullCalls);
  assert(sensor.diagnostics().rxQueueFull==1);
  pinInput[INT]=LOW; // Queued RX is preferred even with more hardware data ready.
  assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==6 && buffer[3]==0);
  assert(SPI.transferCalls==fullCalls);
  prepare(packet(6,false,4));
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==9);
  for(unsigned i=1;i<5;++i) {
    assert(hal->read(hal,buffer,sizeof(buffer),&stamp)==6 && buffer[3]==i);
  }
  assert(sensor.queuedPackets()==0);

  // Missing INT is a negative error, so SHTP does not spin retrying forever.
  pinInput[INT]=HIGH;
  const unsigned callsBefore=SPI.transferCalls;
  const uint32_t timeoutStart=micros();
  events.clear();
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))<0);
  assert(micros()-timeoutStart>=50000 && micros()-timeoutStart<=51000);
  assert(events==std::vector<std::string>({"wake-low","wake-high"}));
  assert(SPI.transferCalls==callsBefore && sensor.hardwareResetCalls==2);
  assert(pinDuty[WAKE]==HIGH && pinDuty[CS]==HIGH && !SPI.transactionActive);
  assert(hal->write(hal,nullptr,5)<0);
  assert(hal->write(hal,buffer,0)<0);
  assert(hal->write(hal,buffer,3)<0);
  assert(hal->write(hal,buffer,SH2_HAL_MAX_TRANSFER_OUT+1)<0);
  assert(SPI.transferCalls==callsBefore);
  fakeNow=UINT32_MAX-20;
  const uint32_t rolloverStart=millis();
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))<0);
  assert(static_cast<uint32_t>(millis()-rolloverStart)>=50);
  assert(static_cast<uint32_t>(millis()-rolloverStart)<=51);
  assert(sensor.diagnostics().wakeTimeouts==2);

  // One SH-2 service pass may decode all three reports: deliver each once, in
  // order, including reports emitted during report setup before first polling.
  emitEvent(SH2_GAME_ROTATION_VECTOR);
  sh2_SensorValue_t value;
  assert(sensor.getSensorEvent(&value) && value.sensorId==SH2_GAME_ROTATION_VECTOR);
  fakeSh2ServiceHook=bundledReports;
  const uint32_t arrival=millis();
  assert(sensor.getSensorEvent(&value) && value.sensorId==SH2_GAME_ROTATION_VECTOR);
  assert(serviceCalls==1 && sensor.queuedSensorEvents()==2);
  delay(700);
  assert(sensor.getSensorEvent(&value) && value.sensorId==SH2_GYROSCOPE_CALIBRATED);
  assert(sensor.lastEventReceivedMs()==arrival);
  assert(sensor.getSensorEvent(&value) && value.sensorId==SH2_LINEAR_ACCELERATION);
  assert(sensor.lastEventReceivedMs()==arrival && serviceCalls==1);
  assert(!sensor.getSensorEvent(&value) && sensor.queuedSensorEvents()==0);
  assert(!sensor.getSensorEvent(nullptr));

  for(unsigned i=0;i<35;++i) emitEvent(SH2_GYROSCOPE_CALIBRATED,static_cast<float>(i));
  assert(sensor.queuedSensorEvents()==32 && sensor.diagnostics().sensorQueueDrops==3);
  for(unsigned i=0;i<32;++i) {
    assert(sensor.getSensorEvent(&value));
    assert(value.un.gyroscope.x==static_cast<float>(i));
  }
  emitEvent(SH2_LINEAR_ACCELERATION);
  sensor.resetPending=true;
  assert(sensor.wasReset() && sensor.queuedSensorEvents()==0);
  assert(!sensor.wasReset());

  // Retrying begin clears old packets/reports but keeps diagnostic evidence.
  prepare(packet(6));
  assert(hal->write(hal,tx.data(),static_cast<unsigned>(tx.size()))==9);
  emitEvent(SH2_LINEAR_ACCELERATION);
  const auto diagnostics=sensor.diagnostics();
  pinInput[INT]=HIGH;
  const uint32_t failedStart=millis();
  assert(!sensor.begin_SPI(CS,INT,&SPI));
  assert(static_cast<uint32_t>(millis()-failedStart)==500);
  assert(sensor.hardwareResetCalls==3 && sensor.nativeOpenCalls==0);
  assert(fakeSh2Allocations==3 && fakeSh2CloseCalls==3);
  assert(!fakeShtpSlot && !fakeSh2Session && pinDuty[WAKE]==HIGH);
  assert(sensor.queuedPackets()==0 && sensor.queuedSensorEvents()==0);
  assert(sensor.diagnostics().rxPackets==diagnostics.rxPackets);
  assert(sensor.diagnostics().sensorEvents==diagnostics.sensorEvents);
  pinInput[INT]=LOW;
  assert(sensor.begin_SPI(CS,INT,&SPI));
  assert(sensor.hardwareResetCalls==4);
  prepare(packet(7));
  assert(sensor.testHal()->read(sensor.testHal(),buffer,sizeof(buffer),&stamp)==7);

  // Product-ID failure after successful HAL open also owns a slot.
  pinInput[INT]=LOW;
  sensor.beginSucceeds=false;
  assert(!sensor.begin_SPI(CS,INT,&SPI));
  assert(fakeSh2Allocations==5 && fakeSh2CloseCalls==5);
  assert(!fakeShtpSlot && !fakeSh2Session);
  sensor.beginSucceeds=true;
  for(unsigned retry=0;retry<3;++retry) {
    pinInput[INT]=LOW;
    assert(sensor.begin_SPI(CS,INT,&SPI));
    assert(fakeShtpSlot==sensor.testHal() && fakeSh2Session==sensor.testHal());
    assert(fakeSh2Allocations==6+retry && fakeSh2CloseCalls==5+retry);
    prepare(packet(7));
    assert(sensor.testHal()->read(sensor.testHal(),buffer,sizeof(buffer),&stamp)==7);
  }
  const unsigned closeCalls=fakeSh2CloseCalls;
  assert(!sensor.begin_SPI(CS,27,&SPI));
  assert(fakeSh2CloseCalls==closeCalls && fakeSh2Session==sensor.testHal());
  pinInput[INT]=LOW;
  sensor.beginSucceeds=false;
  assert(!sensor.begin_SPI(CS,INT,&SPI));
  assert(fakeSh2Allocations==9 && fakeSh2CloseCalls==9);
  assert(!fakeShtpSlot && !fakeSh2Session);
  assert(fakeSh2NullCloseCalls==0 && fakeBeginWithLiveSession==0);
  assert(sensor.nativeReadCalls==0 && sensor.nativeWriteCalls==0 && SPI.writeCalls==0);
  std::cout << "PASS: SPI continuous CS with early INT release, full-duplex RX/TX and zero padding, valid/null/malformed bounds, FIFO backpressure/order, negative wake timeout and rollover, all bundled reports and arrival timestamps, reset/retry cleanup, and repeated single-slot reinitialization\n";
}
