#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "../Maker_IMU_UART_RVC_Check/RvcParser.h"

using Frame = std::array<uint8_t,19>;
const Frame official = {0xAA,0xAA,0xDE,0x01,0x00,0x92,0xFF,0x25,0x08,
                       0x8D,0xFE,0xEC,0xFF,0xD1,0x03,0x00,0x00,0x00,0xE7};
void checksum(Frame& bytes) {
  uint16_t sum=0;
  for(unsigned i=2;i<18;++i) sum+=bytes[i];
  bytes[18]=static_cast<uint8_t>(sum);
}
Frame frame(uint8_t index,const std::array<int16_t,6>& values={1,-2,3,-4,5,-6}) {
  Frame bytes={0xAA,0xAA,index};
  for(unsigned i=0;i<values.size();++i) {
    const uint16_t raw=static_cast<uint16_t>(values[i]);
    bytes[3+i*2]=static_cast<uint8_t>(raw);
    bytes[4+i*2]=static_cast<uint8_t>(raw>>8);
  }
  checksum(bytes);
  return bytes;
}
unsigned feed(RvcParser& parser,const Frame& bytes) {
  unsigned accepted=0;
  for(uint8_t byte:bytes) if(parser.feed(byte)) ++accepted;
  return accepted;
}
unsigned feed(RvcParser& parser,const std::vector<uint8_t>& bytes) {
  unsigned accepted=0;
  for(uint8_t byte:bytes) if(parser.feed(byte)) ++accepted;
  return accepted;
}

int main() {
  RvcParser example;
  for(unsigned i=0;i<official.size();++i)
    assert(example.feed(official[i])==(i==official.size()-1));
  const auto sample=example.sample();
  assert(sample.index==0xDE && sample.yaw==1 && sample.pitch==-110 && sample.roll==2085);
  assert(sample.ax==-371 && sample.ay==-20 && sample.az==977);
  assert(example.validFrames()==1 && example.badChecksums()==0);
  assert(example.indexDiscontinuities()==0);

  RvcParser extremes;
  assert(feed(extremes,frame(0,{INT16_MIN,INT16_MAX,-1,0,INT16_MIN,INT16_MAX}))==1);
  assert(extremes.sample().yaw==INT16_MIN && extremes.sample().pitch==INT16_MAX);
  assert(extremes.sample().roll==-1 && extremes.sample().ax==0);
  assert(extremes.sample().ay==INT16_MIN && extremes.sample().az==INT16_MAX);

  // Every possible caller chunk boundary preserves partial parser state.
  for(unsigned split=0;split<=official.size();++split) {
    RvcParser splitParser;
    unsigned accepted=0;
    for(unsigned i=0;i<split;++i) accepted+=splitParser.feed(official[i])?1U:0U;
    assert(accepted==(split==official.size()?1U:0U));
    for(unsigned i=split;i<official.size();++i) accepted+=splitParser.feed(official[i])?1U:0U;
    assert(accepted==1 && splitParser.sample().index==0xDE);
  }

  RvcParser sequence;
  for(uint8_t index: {uint8_t{254},uint8_t{255},uint8_t{0},uint8_t{1}})
    assert(feed(sequence,frame(index))==1);
  assert(sequence.indexDiscontinuities()==0);
  assert(feed(sequence,frame(1))==1); // Duplicate, not an inferred lost-frame count.
  assert(feed(sequence,frame(7))==1);
  assert(feed(sequence,frame(6))==1);
  assert(feed(sequence,frame(7))==1);
  assert(sequence.indexDiscontinuities()==3 && sequence.validFrames()==8);

  // Noise and isolated AA bytes must not cause callbacks or checksum errors.
  RvcParser noisy;
  assert(feed(noisy,std::vector<uint8_t>{0,1,2,0xAA,0,0xAA,1,0x55,0xFF})==0);
  assert(noisy.validFrames()==0 && noisy.badChecksums()==0);
  assert(feed(noisy,official)==1);
  const auto saved=noisy.sample();
  auto corrupted=frame(0xDF);
  corrupted[18]^=1;
  assert(feed(noisy,corrupted)==0);
  assert(noisy.badChecksums()==1 && noisy.sample().index==saved.index);
  assert(feed(noisy,frame(0xE0))==1);
  assert(noisy.indexDiscontinuities()==1); // Invalid frame does not update index.

  // Truncated frames can swallow part of the next header before checksum fails.
  // Retaining the failed candidate's suffix recovers the entire next frame.
  const auto damaged=frame(40);
  const auto next=frame(41);
  for(unsigned prefix=1;prefix<damaged.size();++prefix) {
    RvcParser truncated;
    for(unsigned i=0;i<prefix;++i) assert(!truncated.feed(damaged[i]));
    assert(feed(truncated,next)==1);
    assert(truncated.sample().index==41 && truncated.validFrames()==1);
  }

  // An extra leading AA creates an overlapping header candidate at offset1.
  RvcParser overlapping;
  assert(!overlapping.feed(0xAA));
  assert(feed(overlapping,official)==1);
  assert(overlapping.validFrames()==1 && overlapping.badChecksums()==1);
  assert(overlapping.sample().index==0xDE);

  // AA AA inside a valid payload/index/reserved region is data, not a restart.
  auto embedded=frame(0xAA,{-21846,170,-21846,-1,INT16_MIN,INT16_MAX});
  embedded[15]=0xAA;
  embedded[16]=0xAA;
  embedded[17]=0xAA;
  checksum(embedded);
  RvcParser payload;
  assert(feed(payload,embedded)==1);
  assert(payload.sample().index==0xAA && payload.sample().yaw==-21846);
  assert(payload.sample().pitch==170 && payload.sample().roll==-21846);
  assert(payload.badChecksums()==0);
  auto badEmbedded=embedded;
  badEmbedded[18]^=1;
  assert(feed(payload,badEmbedded)==0);
  assert(feed(payload,frame(0xAB))==1);
  assert(payload.sample().index==0xAB && payload.validFrames()==2);
  assert(payload.indexDiscontinuities()==0);

  // Reserved bytes participate in the checksum, and checksum arithmetic wraps.
  auto reserved=frame(8);
  reserved[15]=0xF0; reserved[16]=0xF1; reserved[17]=0xF2;
  checksum(reserved);
  RvcParser wrapped;
  assert(feed(wrapped,reserved)==1);
  reserved[16]^=1;
  assert(feed(wrapped,reserved)==0);
  assert(wrapped.badChecksums()==1);

  // A corrupt candidate ending in AA must retain it as a possible next header.
  auto endsInHeader=frame(10);
  assert(endsInHeader[18]!=0xAA);
  endsInHeader[18]=0xAA;
  RvcParser trailing;
  assert(feed(trailing,endsInHeader)==0);
  const auto last=frame(11);
  unsigned count=0;
  for(unsigned i=1;i<last.size();++i) count+=trailing.feed(last[i])?1U:0U;
  assert(count==1 && trailing.sample().index==11);

  std::cout << "PASS: official UART-RVC example, signed extremes, split input, index wrap/discontinuities, noise/checksum rejection, truncated and overlapping-header recovery, embeddedAA payload and reserved-byte checksum\n";
}
