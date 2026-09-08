#pragma once
#include "Arduino.h"
#include <vector>
#include <deque>
struct TwoWire {
  bool ack = false, stream = false, started = false;
  int sda = -1, scl = -1;
  unsigned clock = 0, timeout = 0, buffer = 0;
  unsigned beginCalls = 0, endCalls = 0;
  uint8_t seq = 0;
  uint8_t sensor = 1;
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  std::deque<std::vector<uint8_t>> packets;
  bool begin(int a,int b,unsigned hz) {sda=a;scl=b;clock=hz;started=true;stream=false;packets.clear();++beginCalls;return true;}
  void end() {started=false;++endCalls;}
  void setTimeOut(unsigned n) {timeout=n;}
  size_t setBufferSize(size_t n) {buffer=n;return n;}
  void beginTransmission(uint8_t) {tx.clear();}
  size_t write(const uint8_t *p,size_t n) {tx.assign(p,p+n);return n;}
  int endTransmission() {
    if (!ack) return 2;
    if (tx.size() == 21 && tx[4] == 0xFD) {
      stream=true;
      sensor=tx[5];
      auto reply=tx; reply[3]=seq++;reply[4]=0xFC;packets.push_back(reply);
    }
    return 0;
  }
  size_t requestFrom(uint8_t,size_t n,bool) {
    rx.clear();
    if (packets.empty() && stream) {
      std::vector<uint8_t> p(19,0); p[0]=19;p[2]=3;p[3]=seq++;p[4]=0xFB;p[9]=sensor;
      packets.push_back(p);
    }
    if (packets.empty()) {rx.assign(4,0);return 4;}
    const auto &p=packets.front();
    size_t count=std::min(n,p.size());
    rx.assign(p.begin(),p.begin()+count);
    if(n!=4 && count>=4) {rx[1]|=0x80;rx[3]=uint8_t(rx[3]+1);} // Continuation increments sequence.
    if (n != 4) packets.pop_front();
    return count;
  }
  int available() {return static_cast<int>(rx.size());}
  int read() {if(rx.empty())return -1;auto b=rx.front();rx.pop_front();return b;}
};
inline TwoWire Wire;
