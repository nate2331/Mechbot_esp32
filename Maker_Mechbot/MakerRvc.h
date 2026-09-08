#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp32-hal-uart.h>
#include <atomic>
#include "RvcParser.h"

// Receive-only adapter for the established buffered GPIO21 circuit. No reset,
// mode, power or TX pin is driven. Qualification is not calibration confidence.
class MakerRvc {
 public:
  bool begin(uint32_t now) {
    if (started_) Serial1.end();
    started_=true;
    parser_=RvcParser{};
    ready_=accepted_=haveFrame_=discard_=false;
    streak_=0; lastPoll_=now; lastFrame_=now;
    badChecksums_=discontinuities_=0;
    uartErrors_.store(0); seenUartErrors_=0;
    pinMode(21,INPUT);
    const auto allocated=Serial1.setRxBufferSize(8192);
    Serial1.begin(115200,SERIAL_8N1,21,-1);
    available_=Serial1 && allocated>=8192 && uart_get_RxPin(1)==21 && uart_get_TxPin(1)==-1;
    if (available_) Serial1.onReceiveError(onError);
    else { Serial1.end(); pinMode(21,INPUT); }
    return available_;
  }

  void poll(uint32_t now) {
    if (!available_) return;
    const auto errors=uartErrors_.load(std::memory_order_relaxed);
    if (now-lastPoll_>100 || Serial1.available()>76 || errors!=seenUartErrors_) {
      invalidate(); discard_=true;
    }
    seenUartErrors_=errors;
    lastPoll_=now;
    if (haveFrame_ && now-lastFrame_>500) invalidate();
    // Never re-date a large queued backlog as a fresh heading after a stalled loop.
    // Drain across calls if necessary, while leaving time for motor commands.
    if (discard_) {
      for(unsigned n=0;n<1024 && Serial1.available();++n) Serial1.read();
      if (!Serial1.available()) { parser_=RvcParser{}; haveFrame_=false; discard_=false; }
      return;
    }
    for(unsigned n=0;n<1024 && Serial1.available();++n) {
      const int byte=Serial1.read();
      if (byte<0) break;
      const auto bad=parser_.badChecksums();
      const auto gaps=parser_.indexDiscontinuities();
      if (!parser_.feed(static_cast<uint8_t>(byte))) {
        if (parser_.badChecksums()!=bad) { ++badChecksums_; invalidate(); }
        continue;
      }
      const bool consecutive=haveFrame_ && parser_.indexDiscontinuities()==gaps && now-lastFrame_<=100;
      if (parser_.indexDiscontinuities()!=gaps) { ++discontinuities_; invalidate(); }
      streak_=consecutive ? (streak_<5 ? streak_+1 : 5) : 1;
      if (!consecutive && haveFrame_) invalidateAcceptance();
      lastFrame_=now; haveFrame_=true;
      ready_=streak_>=5;
    }
  }

  bool available() const { return available_; }
  bool fresh(uint32_t now) const {
    return available_ && ready_ && haveFrame_ && now-lastFrame_<=500 && now-lastPoll_<=100 &&
      uartErrors_.load(std::memory_order_relaxed)==seenUartErrors_ && Serial1.available()<=76;
  }
  bool accepted(uint32_t now) const { return accepted_ && fresh(now); }
  bool accept(uint32_t now) { accepted_=fresh(now); return accepted_; }
  void revoke() { accepted_=false; }
  const char* state(uint32_t now) const {
    return !available_ ? "OFFLINE" : fresh(now) ? "READY" : haveFrame_ && now-lastFrame_>500 ? "STALE" : "SYNCING";
  }
  int64_t age(uint32_t now) const { return haveFrame_ ? static_cast<int64_t>(uint32_t(now-lastFrame_)) : -1; }
  const RvcParser::Sample& sample() const { return parser_.sample(); }
  uint32_t badChecksums() const { return badChecksums_; }
  uint32_t discontinuities() const { return discontinuities_; }
  uint32_t uartErrors() const { return uartErrors_.load(std::memory_order_relaxed); }

 private:
  RvcParser parser_;
  bool started_=false, available_=false, ready_=false, accepted_=false, haveFrame_=false, discard_=false;
  uint32_t streak_=0,lastPoll_=0,lastFrame_=0,seenUartErrors_=0,badChecksums_=0,discontinuities_=0;
  inline static std::atomic<uint32_t> uartErrors_{0};
  static void onError(hardwareSerial_error_t error) {
    if (error!=UART_NO_ERROR) uartErrors_.fetch_add(1,std::memory_order_relaxed);
  }
  void invalidateAcceptance() { accepted_=false; }
  void invalidate() { ready_=false; accepted_=false; streak_=0; }
};
