// Diagnostic tracing is bounded RAM-only work outside the active SPI exchange.
#include <cassert>
#include <iostream>
#include <vector>
#include "../Maker_Mechbot_SPI_Diag/MakerBnoSpi.h"

constexpr unsigned RESET=25, WAKE=16, INT=26, CS=33;
std::vector<uint8_t> incoming, transmitted;
std::vector<unsigned> burstSizes;
unsigned cursor=0, csAssertions=0, csReleases=0;
bool stampRelease=false;

void pinHook(unsigned pin,unsigned value) {
  if(pin!=CS) return;
  if(value==LOW) {
    assert(SPI.transactionActive);
    ++csAssertions;
    cursor=0;
    transmitted.clear();
    burstSizes.clear();
    pinInput[INT]=HIGH;
  } else {
    ++csReleases;
    if(stampRelease) delay(2);
  }
}
void transferHook(const uint8_t* tx,uint8_t* rx,uint32_t count) {
  assert(pinDuty[CS]==LOW && SPI.transactionActive);
  assert(SPI.lastSettings.clock==1000000 && SPI.lastSettings.mode==SPI_MODE3);
  assert(SPI.lastSettings.order==MSBFIRST);
  assert(reinterpret_cast<uintptr_t>(tx)%4==0);
  assert(reinterpret_cast<uintptr_t>(rx)%4==0);
  if(!cursor) {
    const unsigned declared=(static_cast<unsigned>(tx[0]) |
                             (static_cast<unsigned>(tx[1])<<8)) & 0x7FFFU;
    assert(count==(declared?declared:4));
  }
  burstSizes.push_back(count);
  for(unsigned i=0;i<count;++i,++cursor) {
    transmitted.push_back(tx[i]);
    rx[i]=cursor<incoming.size()?incoming[cursor]:0xFF;
  }
}
std::vector<uint8_t> packet(unsigned length,uint8_t reportId=0xFC) {
  assert(length>=4 && length<=SH2_HAL_MAX_TRANSFER_IN);
  std::vector<uint8_t> bytes(length,0xA5);
  bytes[0]=static_cast<uint8_t>(length);
  bytes[1]=static_cast<uint8_t>(length>>8);
  bytes[2]=2;
  bytes[3]=9;
  if(length>4) bytes[4]=reportId;
  return bytes;
}
void prepare(const std::vector<uint8_t>& bytes) {
  incoming=bytes;
  pinInput[INT]=LOW;
}
void samePrefix(const MakerBnoSpi::TraceEntry* entry,char direction,
                unsigned length,const std::vector<uint8_t>& expected) {
  assert(entry && entry->direction==direction && entry->length==length);
  assert(entry->prefixLength==expected.size());
  assert(std::vector<uint8_t>(entry->prefix,entry->prefix+entry->prefixLength)==expected);
}
void traceDuringFailedInit(sh2_Hal_t* hal) {
  uint8_t tx[]={6,0,2,0,0xF9,0};
  prepare(packet(20,0xF8));
  assert(hal->write(hal,tx,sizeof(tx))==6);
}

int main() {
  MakerBnoSpi sensor(RESET,WAKE,INT);
  digitalWriteHook=pinHook;
  spiTransferHook=transferHook;
  SPI.begin(22,21,32,CS);
  digitalWrite(CS,HIGH);
  pinInput[INT]=LOW;
  assert(sensor.begin_SPI(CS,INT,&SPI));
  assert(sensor.traceCount()==0 && sensor.traceOverflowCount()==0);
  assert(sensor.traceAt(0)==nullptr && sensor.traceAt(UINT32_MAX)==nullptr);
  sh2_Hal_t* hal=sensor.testHal();
  uint8_t readBuffer[SH2_HAL_MAX_TRANSFER_IN]={};
  uint32_t receivedUs=0;

  // Preserve the complete feature command and its duplex response, in order.
  std::vector<uint8_t> feature={0x15,0,2,1,0xFD,8,0,0,0,0x20,0x4E,0,0,0,0,0,0,0,0,0,0};
  auto reply=packet(21,0xFC);
  prepare(reply);
  fakeNow=123;
  stampRelease=true;
  const unsigned starts=csAssertions,ends=csReleases;
  assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
  stampRelease=false;
  assert(csAssertions==starts+1 && csReleases==ends+1);
  assert(transmitted==feature && sensor.traceCount()==2);
  assert(burstSizes==std::vector<unsigned>({21}));
  samePrefix(sensor.traceAt(0),'T',21,feature);
  samePrefix(sensor.traceAt(1),'R',21,reply);
  assert(sensor.traceAt(0)->timestampMs==123);
  assert(sensor.traceAt(1)->timestampMs==125);
  assert(!SPI.transactionActive && pinDuty[CS]==HIGH && pinDuty[WAKE]==HIGH);
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==21);
  assert(sensor.traceCount()==2); // FIFO delivery is not another bus packet.

  // Large RX records retain the original length and only the first32 bytes.
  const auto longReply=packet(SH2_HAL_MAX_TRANSFER_IN,0xF1);
  prepare(longReply);
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==SH2_HAL_MAX_TRANSFER_IN);
  assert(burstSizes==std::vector<unsigned>({4,SH2_HAL_MAX_TRANSFER_IN-4}));
  samePrefix(sensor.traceAt(2),'R',SH2_HAL_MAX_TRANSFER_IN,
             std::vector<uint8_t>(longReply.begin(),longReply.begin()+32));

  // Null reads occupy no trace slots; malformed RX retains only its header.
  prepare({0,0,0,0});
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==0);
  assert(sensor.traceCount()==3);
  prepare({0xFF,0xFF,0xA0,0xB0});
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==0);
  samePrefix(sensor.traceAt(3),'R',0x7FFF,{0xFF,0xFF,0xA0,0xB0});
  prepare({2,0,0xF1,0});
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==0);
  samePrefix(sensor.traceAt(4),'R',2,{2,0,0xF1,0});

  // TX remains meaningful even when its duplex RX is null. Retain the first
  //48 records unchanged, counting every later meaningful packet as overflow.
  const auto savedFirst=*sensor.traceAt(0);
  unsigned writeIndex=0;
  while(sensor.traceCount()<MakerBnoSpi::TRACE_CAPACITY) {
    feature[3]=static_cast<uint8_t>(writeIndex++);
    prepare({0,0,0,0});
    assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
  }
  assert(sensor.traceCount()==48 && sensor.traceOverflowCount()==0);
  assert(sensor.traceAt(48)==nullptr);
  prepare(packet(5,0xF1));
  assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
  assert(sensor.traceCount()==48 && sensor.traceOverflowCount()==2);
  assert(sensor.traceAt(0)->timestampMs==savedFirst.timestampMs);
  samePrefix(sensor.traceAt(0),'T',21,
             std::vector<uint8_t>(savedFirst.prefix,savedFirst.prefix+savedFirst.prefixLength));
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==5);
  prepare({0,0,0,0});
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==0);
  assert(sensor.traceOverflowCount()==2);

  // An init failure keeps packets captured inside that attempt after close.
  sensor.afterOpenHook=traceDuringFailedInit;
  sensor.beginSucceeds=false;
  pinInput[INT]=LOW;
  assert(!sensor.begin_SPI(CS,INT,&SPI));
  assert(sensor.traceCount()==2 && sensor.traceOverflowCount()==0);
  samePrefix(sensor.traceAt(0),'T',6,{6,0,2,0,0xF9,0});
  assert(sensor.traceAt(1)->direction=='R' && sensor.traceAt(1)->prefix[4]==0xF8);
  assert(!fakeSh2Session && !fakeShtpSlot);
  assert(!sensor.begin_SPI(CS,INT,nullptr));
  assert(sensor.traceCount()==2); // Rejected argument is not a new attempt.

  // The next real attempt clears prior records and overflow state.
  sensor.afterOpenHook=nullptr;
  sensor.beginSucceeds=true;
  pinInput[INT]=LOW;
  assert(sensor.begin_SPI(CS,INT,&SPI));
  assert(sensor.traceCount()==0 && sensor.traceOverflowCount()==0);
  assert(sensor.traceAt(0)==nullptr);
  const unsigned transfers=SPI.transferCalls;
  pinInput[INT]=HIGH;
  assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))<0);
  assert(sensor.traceCount()==0 && SPI.transferCalls==transfers);

  // Raw manual queries continue the actual channel2 wire sequence after the
  // library's three feature writes, preserving full-duplex responses in trace.
  for(uint8_t sequence=1;sequence<=3;++sequence) {
    feature[3]=sequence;
    prepare({0,0,0,0});
    assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
  }
  prepare(packet(21,0xFC));
  assert(sensor.probeControl(0xFE,0x08));
  samePrefix(sensor.traceAt(3),'T',6,{6,0,2,4,0xFE,8});
  assert(sensor.traceAt(4)->direction=='R' && sensor.traceAt(4)->prefix[4]==0xFC);
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==21);
  prepare(packet(20,0xF8));
  assert(sensor.probeControl(0xF9,0));
  samePrefix(sensor.traceAt(5),'T',6,{6,0,2,5,0xF9,0});
  assert(sensor.traceAt(6)->direction=='R' && sensor.traceAt(6)->prefix[4]==0xF8);
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==20);

  const unsigned beforeInvalid=SPI.transferCalls,traceBeforeInvalid=sensor.traceCount();
  const uint32_t invalidAt=micros();
  assert(!sensor.probeControl(0xFD,8));
  assert(!sensor.probeControl(0xFE,2));
  assert(!sensor.probeControl(0xF9,1));
  assert(SPI.transferCalls==beforeInvalid && sensor.traceCount()==traceBeforeInvalid);
  assert(micros()==invalidAt);

  // A missing wake interrupt is bounded and does not consume a sequence.
  pinInput[INT]=HIGH;
  const uint32_t timeoutAt=micros();
  assert(!sensor.probeControl(0xFE,8));
  assert(micros()-timeoutAt>=50000 && micros()-timeoutAt<=51000);
  assert(SPI.transferCalls==beforeInvalid && sensor.traceCount()==traceBeforeInvalid);
  prepare({0,0,0,0});
  assert(sensor.probeControl(0xFE,8));
  samePrefix(sensor.traceAt(7),'T',6,{6,0,2,6,0xFE,8});

  // Writes to other channels must not alter the control channel sequence.
  uint8_t otherChannel[]={5,0,1,99,1};
  prepare({0,0,0,0});
  assert(hal->write(hal,otherChannel,sizeof(otherChannel))==5);
  prepare({0,0,0,0});
  assert(sensor.probeControl(0xF9,0));
  samePrefix(sensor.traceAt(9),'T',6,{6,0,2,7,0xF9,0});

  // Full RX queue is one unsuccessful attempt, not a hidden retry loop. The
  // eventual query keeps the sequence following the last completed write.
  for(uint8_t sequence=10;sequence<=13;++sequence) {
    feature[3]=sequence;
    prepare(packet(5));
    assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
  }
  assert(sensor.queuedPackets()==4);
  const uint32_t fullAt=micros();
  const unsigned beforeFull=SPI.transferCalls,traceBeforeFull=sensor.traceCount();
  assert(!sensor.probeControl(0xFE,8));
  assert(micros()==fullAt && SPI.transferCalls==beforeFull);
  assert(sensor.traceCount()==traceBeforeFull);
  assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==5);
  prepare({0,0,0,0});
  assert(sensor.probeControl(0xFE,8));
  samePrefix(sensor.traceAt(traceBeforeFull),'T',6,{6,0,2,14,0xFE,8});
  while(sensor.queuedPackets()) assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==5);

  // Wire sequence wraps modulo256 and a fresh initialization restarts at zero.
  feature[3]=255;
  prepare({0,0,0,0});
  assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
  prepare({0,0,0,0});
  assert(sensor.probeControl(0xF9,0));
  samePrefix(sensor.traceAt(sensor.traceCount()-1),'T',6,{6,0,2,0,0xF9,0});
  pinInput[INT]=LOW;
  assert(sensor.begin_SPI(CS,INT,&SPI));
  prepare({0,0,0,0});
  assert(sensor.probeControl(0xFE,8));
  samePrefix(sensor.traceAt(0),'T',6,{6,0,2,0,0xFE,8});

  // The A/B changes only TX burst shape. A21-byte command is one complete
  // first transfer; null/short/invalid RX adds no call, longer RX adds only its
  // remaining bytes with zero MOSI padding and no intervening CS release.
  for(const auto& response: {std::vector<uint8_t>{0,0,0,0},packet(20),
                            packet(52),packet(276),
                            std::vector<uint8_t>{0xFF,0xFF,0xA0,0xB0}}) {
    const unsigned declared=(static_cast<unsigned>(response[0]) |
                             (static_cast<unsigned>(response[1])<<8)) & 0x7FFFU;
    const bool valid=declared>=4 && declared<=SH2_HAL_MAX_TRANSFER_IN;
    const unsigned expectedClocks=valid && declared>21?declared:21;
    const unsigned oldCsAssertions=csAssertions,oldCsReleases=csReleases;
    const unsigned oldTrace=sensor.traceCount();
    const auto originalFeature=feature;
    prepare(response);
    assert(hal->write(hal,feature.data(),static_cast<unsigned>(feature.size()))==21);
    assert(csAssertions==oldCsAssertions+1 && csReleases==oldCsReleases+1);
    assert(!SPI.transactionActive && pinDuty[CS]==HIGH && pinDuty[WAKE]==HIGH);
    assert(burstSizes.front()==21);
    assert(burstSizes.size()==(expectedClocks>21?2U:1U));
    if(expectedClocks>21) assert(burstSizes[1]==expectedClocks-21);
    std::vector<uint8_t> expectedMosi=feature;
    expectedMosi.resize(expectedClocks,0);
    assert(transmitted==expectedMosi && feature==originalFeature);
    samePrefix(sensor.traceAt(oldTrace),'T',21,feature);
    if(valid) {
      assert(sensor.queuedPackets()==1);
      const unsigned callsBeforeDrain=SPI.transferCalls;
      assert(hal->read(hal,readBuffer,sizeof(readBuffer),&receivedUs)==static_cast<int>(declared));
      assert(std::vector<uint8_t>(readBuffer,readBuffer+declared)==response);
      assert(sensor.queuedPackets()==0 && SPI.transferCalls==callsBeforeDrain);
    } else {
      assert(sensor.queuedPackets()==0);
    }
  }
  assert(Serial.output.empty());
  assert(fakeSh2NullCloseCalls==0);
  std::cout << "PASS: diagnostic trace/probe bounds and sequence; whole21-byte TX with null/20/52/276/invalid RX, aligned tail buffers, zero padding, continuous CS, complete RX retention and unchanged read framing\n";
}
