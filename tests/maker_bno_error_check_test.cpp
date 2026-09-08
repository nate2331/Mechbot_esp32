#include <cassert>
#include <iostream>
#include <vector>
#include "../Maker_IMU_Error_Check/MakerBnoSpi.h"

constexpr unsigned RESET=25, WAKE=16, INT=26, CS=33;
std::vector<uint8_t> incoming, transmitted;
std::vector<unsigned> bursts;
unsigned cursor=0, assertions=0, releases=0;
void pinHook(unsigned pin,unsigned value) {
  if(pin!=CS) return;
  if(value==LOW) {
    assert(SPI.transactionActive);
    ++assertions;
    cursor=0;
    transmitted.clear();
    bursts.clear();
    pinInput[INT]=HIGH;
  } else ++releases;
}
void transferHook(const uint8_t* tx,uint8_t* rx,uint32_t count) {
  assert(pinDuty[CS]==LOW && SPI.transactionActive);
  assert(SPI.lastSettings.clock==1000000 && SPI.lastSettings.mode==SPI_MODE3);
  assert(SPI.lastSettings.order==MSBFIRST);
  assert(reinterpret_cast<uintptr_t>(tx)%4==0 && reinterpret_cast<uintptr_t>(rx)%4==0);
  bursts.push_back(count);
  for(unsigned i=0;i<count;++i,++cursor) {
    transmitted.push_back(tx[i]);
    rx[i]=cursor<incoming.size()?incoming[cursor]:0xFF;
  }
}
void prepare(const std::vector<uint8_t>& bytes={0,0,0,0}) {
  incoming=bytes;
  pinInput[INT]=LOW;
}
std::vector<uint8_t> response(uint8_t request,uint8_t source,uint8_t marker=7) {
  return {0xF1,9,1,request,0,0,marker,source,11,12,13,0,0,0,0,0};
}
std::vector<uint8_t> packet(const std::vector<std::vector<uint8_t>>& records,
                            uint8_t channel=2,bool continuation=false) {
  std::vector<uint8_t> bytes={0,0,channel,0};
  for(const auto& record:records) bytes.insert(bytes.end(),record.begin(),record.end());
  assert(bytes.size()<=SH2_HAL_MAX_TRANSFER_IN);
  bytes[0]=static_cast<uint8_t>(bytes.size());
  bytes[1]=static_cast<uint8_t>((bytes.size()>>8)|(continuation?0x80:0));
  return bytes;
}
void receive(MakerBnoSpi& sensor,const std::vector<uint8_t>& bytes) {
  prepare(bytes);
  uint8_t buffer[SH2_HAL_MAX_TRANSFER_IN]={};
  uint32_t timestamp=0;
  auto* hal=sensor.testHal();
  assert(hal->read(hal,buffer,sizeof(buffer),&timestamp)==static_cast<int>(bytes.size()));
  assert(std::vector<uint8_t>(buffer,buffer+bytes.size())==bytes);
}

int main() {
  MakerBnoSpi sensor(RESET,WAKE,INT);
  digitalWriteHook=pinHook;
  spiTransferHook=transferHook;
  assert(!sensor.probeErrors() && SPI.transferCalls==0);
  assert(sensor.errorAt(0)==nullptr && sensor.errorAt(UINT32_MAX)==nullptr);
  SPI.begin(22,21,32,CS);
  digitalWrite(CS,HIGH);
  prepare();
  assert(sensor.begin_SPI(CS,INT,&SPI));
  // Control and error-command sequences are separate: a product query advances
  // only the former. Error command payload is exactly12bytes plus SHTP header.
  prepare();
  assert(sensor.probeControl(0xF9,0));
  prepare();
  const unsigned started=assertions,ended=releases;
  assert(sensor.probeErrors());
  assert(transmitted==std::vector<uint8_t>({16,0,2,1,0xF2,0,1,0,0,0,0,0,0,0,0,0}));
  assert(bursts==std::vector<unsigned>({16}));
  assert(assertions==started+1 && releases==ended+1);
  assert(sensor.errorRequestSequence()==0 && !sensor.errorResponseComplete());
  assert(sensor.errorCount()==0 && sensor.errorOverflowCount()==0);

  // Unrelated control records can precede multiple errors. The matching errors
  // are beyond the trace prefix and must still be decoded from the full packet.
  std::vector<uint8_t> product(16,0),feature(17,0);
  product[0]=0xF8;
  feature[0]=0xFC;
  auto wrongCommand=response(0,3); wrongCommand[2]=4;
  receive(sensor,packet({product,feature,response(77,3),wrongCommand,
                         response(0,1,22),response(0,4,23)}));
  assert(sensor.errorCount()==2 && !sensor.errorResponseComplete());
  assert(sensor.errorAt(0)->severity==0 && sensor.errorAt(0)->sequence==22);
  assert(sensor.errorAt(0)->source==1 && sensor.errorAt(0)->error==11);
  assert(sensor.errorAt(0)->module==12 && sensor.errorAt(0)->code==13);
  assert(sensor.errorAt(1)->source==4 && sensor.errorAt(1)->sequence==23);
  assert(sensor.errorAt(2)==nullptr);
  receive(sensor,packet({response(0,255)}));
  assert(sensor.errorResponseComplete() && sensor.errorCount()==2);
  receive(sensor,packet({response(0,3)}));
  assert(sensor.errorCount()==2); // Completed requests no longer collect.

  // A new request clears prior data; an empty response completes without adding
  // an error. Full-duplex RX is collected before its queued HAL delivery.
  prepare(packet({response(1,255)}));
  assert(sensor.probeErrors());
  assert(sensor.errorRequestSequence()==1 && sensor.errorResponseComplete());
  assert(sensor.errorCount()==0 && sensor.errorOverflowCount()==0);
  assert(sensor.queuedPackets()==1);
  uint8_t drain[SH2_HAL_MAX_TRANSFER_IN]={};
  uint32_t stamp=0;
  auto* hal=sensor.testHal();
  assert(hal->read(hal,drain,sizeof(drain),&stamp)==20);

  prepare(); assert(sensor.probeErrors());
  assert(sensor.errorRequestSequence()==2 && !sensor.errorResponseComplete());
  auto truncated=response(2,3); truncated.resize(10);
  receive(sensor,packet({truncated}));
  receive(sensor,packet({response(2,255)},3));
  receive(sensor,packet({response(2,255)},2,true));
  receive(sensor,packet({{0xAA,0,0},response(2,255)}));
  receive(sensor,packet({response(99,255)}));
  assert(sensor.errorCount()==0 && !sensor.errorResponseComplete());
  receive(sensor,packet({response(2,3)}));
  assert(sensor.errorCount()==1);

  // Collect the first32 and count later matching records without growing RAM.
  prepare(); assert(sensor.probeErrors());
  const uint8_t request=sensor.errorRequestSequence();
  for(unsigned group=0;group<3;++group) {
    std::vector<std::vector<uint8_t>> records;
    for(unsigned i=0;i<12;++i)
      records.push_back(response(request,3,static_cast<uint8_t>(group*12+i)));
    receive(sensor,packet(records));
  }
  assert(sensor.errorCount()==32 && sensor.errorOverflowCount()==4);
  assert(sensor.errorAt(0)->sequence==0 && sensor.errorAt(31)->sequence==31);
  assert(sensor.errorAt(32)==nullptr);
  receive(sensor,packet({response(request,255)}));
  assert(sensor.errorResponseComplete() && sensor.errorCount()==32);

  // Failed writes return after one bounded attempt and do not advance command
  // sequence; they cannot leave a matching-response collector active.
  const unsigned calls=SPI.transferCalls;
  pinInput[INT]=HIGH;
  const uint32_t began=micros();
  assert(!sensor.probeErrors());
  const uint8_t failedSequence=sensor.errorRequestSequence();
  assert(micros()-began>=50000 && micros()-began<=51000);
  assert(SPI.transferCalls==calls && sensor.errorCount()==0);
  assert(!sensor.errorResponseComplete());
  receive(sensor,packet({response(failedSequence,3),response(failedSequence,255)}));
  assert(sensor.errorCount()==0 && !sensor.errorResponseComplete());
  prepare(); assert(sensor.probeErrors());
  assert(sensor.errorRequestSequence()==failedSequence);

  // Reinitialization clears collector and both raw-command sequence counters.
  prepare(); assert(sensor.begin_SPI(CS,INT,&SPI));
  assert(sensor.errorCount()==0 && sensor.errorOverflowCount()==0);
  assert(!sensor.errorResponseComplete() && sensor.errorRequestSequence()==0);
  prepare(); assert(sensor.probeErrors());
  assert(transmitted==std::vector<uint8_t>({16,0,2,0,0xF2,0,1,0,0,0,0,0,0,0,0,0}));
  assert(Serial.output.empty() && pinDuty[CS]==HIGH && pinDuty[WAKE]==HIGH);
  assert(!SPI.transactionActive && fakeSh2NullCloseCalls==0);
  std::cout << "PASS: bounded error request framing/sequences, full-packet bundled records, matching/empty completion, unrelated/truncated rejection,32-record overflow, timeout and initialization cleanup\n";
}
