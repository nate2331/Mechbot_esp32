#pragma once

#include <Adafruit_BNO08x.h>
#include <cstring>

// Adafruit's SH-2 state is process-global: use one BNO08x instance. Keep its
// decoder, but retain every full-duplex SPI packet and every decoded report.
class MakerBnoSpi : public Adafruit_BNO08x {
 public:
  struct Diagnostics {
    uint32_t rxPackets = 0;
    uint32_t txPackets = 0;
    uint32_t nullHeaders = 0;
    uint32_t badHeaders = 0;
    uint32_t wakeTimeouts = 0;
    uint32_t rxQueueFull = 0;
    uint32_t rxBufferTooSmall = 0;
    uint32_t sensorEvents = 0;
    uint32_t sensorDecodeErrors = 0;
    uint32_t sensorQueueDrops = 0;
  };

  MakerBnoSpi(int8_t resetPin, uint8_t wakePin, uint8_t intPin)
      : Adafruit_BNO08x(resetPin), wakePin_(wakePin), intPin_(intPin) {}

  bool begin_SPI(uint8_t csPin, uint8_t intPin, SPIClass* spi = &SPI,
                 int32_t sensorId = 0) {
    if (intPin != intPin_ || !spi) return false;
    // Only close a session whose allocation was witnessed in HAL.open.
    closeSession();
    rxHead_ = rxCount_ = 0;
    eventHead_ = eventCount_ = 0;
    lastEventReceivedMs_ = 0;
    spi_ = spi;
    csPin_ = csPin;
    return Adafruit_BNO08x::begin_SPI(csPin, intPin, spi, sensorId);
  }

  bool getSensorEvent(sh2_SensorValue_t* value) {
    if (!value || !startupReady_) return false;
    if (popEvent(value)) return true;
    sh2_service();
    return popEvent(value);
  }

  bool wasReset() {
    const bool reset = Adafruit_BNO08x::wasReset();
    if (reset) eventHead_ = eventCount_ = 0;
    // Raw packets may include the new boot's advertisements; SH-2 must see them.
    return reset;
  }

  const Diagnostics& diagnostics() const { return diagnostics_; }
  unsigned queuedPackets() const { return rxCount_; }
  unsigned queuedSensorEvents() const { return eventCount_; }
  uint32_t lastEventReceivedMs() const { return lastEventReceivedMs_; }

  MakerBnoSpi(const MakerBnoSpi&) = delete;
  MakerBnoSpi& operator=(const MakerBnoSpi&) = delete;

 protected:
  bool _init(int32_t sensorId) override {
    pinMode(wakePin_, OUTPUT);
    digitalWrite(wakePin_, HIGH);  // Select SPI before the library's reset.
    active_ = this;
    _HAL.open = openTransport;
    _HAL.read = readTransport;
    _HAL.write = writeTransport;
    _HAL.getTimeUs = timeTransport;
    // SHTP ignores HAL.open's return value. Check startupReady_ independently.
    const bool initialized = Adafruit_BNO08x::_init(sensorId);
    const bool ready = initialized && startupReady_;
    if (!ready || sh2_setSensorCallback(sensorCallback, this) != 0) {
      closeSession();
      return false;
    }
    return true;
  }

 private:
  static constexpr uint32_t STARTUP_TIMEOUT_MS = 500;
  static constexpr uint32_t WAKE_TIMEOUT_MS = 50;
  static constexpr uint32_t SPI_CLOCK_HZ = 1000000;
  static constexpr unsigned RX_QUEUE_CAPACITY = 4;
  static constexpr unsigned EVENT_QUEUE_CAPACITY = 32;
  struct Packet {
    uint32_t receivedUs = 0;
    unsigned len = 0;
    uint8_t data[SH2_HAL_MAX_TRANSFER_IN] = {};
  };
  struct SensorEvent {
    uint32_t receivedMs = 0;
    sh2_SensorValue_t value = {};
  };
  uint8_t wakePin_;
  uint8_t intPin_;
  uint8_t csPin_ = 0;
  SPIClass* spi_ = nullptr;
  bool sessionAllocated_ = false;
  bool startupReady_ = false;
  Packet rxQueue_[RX_QUEUE_CAPACITY];
  SensorEvent eventQueue_[EVENT_QUEUE_CAPACITY];
  unsigned rxHead_ = 0, rxCount_ = 0;
  unsigned eventHead_ = 0, eventCount_ = 0;
  uint32_t lastEventReceivedMs_ = 0;
  Diagnostics diagnostics_;
  uint8_t txScratch_[SH2_HAL_MAX_TRANSFER_IN] = {};
  uint8_t rxScratch_[SH2_HAL_MAX_TRANSFER_IN] = {};
  inline static MakerBnoSpi* active_ = nullptr;

  void closeSession() {
    if (sessionAllocated_) {
      sh2_close();
      sessionAllocated_ = false;
    }
    startupReady_ = false;
  }

  bool popEvent(sh2_SensorValue_t* value) {
    if (!eventCount_) return false;
    const SensorEvent& event = eventQueue_[eventHead_];
    *value = event.value;
    lastEventReceivedMs_ = event.receivedMs;
    eventHead_ = (eventHead_ + 1) % EVENT_QUEUE_CAPACITY;
    --eventCount_;
    return true;
  }

  static void sensorCallback(void* cookie, sh2_SensorEvent_t* event) {
    MakerBnoSpi* device = static_cast<MakerBnoSpi*>(cookie);
    if (!device || !event) return;
    sh2_SensorValue_t value = {};
    if (sh2_decodeSensorEvent(&value, event) != 0) {
      ++device->diagnostics_.sensorDecodeErrors;
      return;
    }
    ++device->diagnostics_.sensorEvents;
    if (device->eventCount_ == EVENT_QUEUE_CAPACITY) {
      ++device->diagnostics_.sensorQueueDrops;
      return;
    }
    SensorEvent& queued = device->eventQueue_[
        (device->eventHead_ + device->eventCount_) % EVENT_QUEUE_CAPACITY];
    queued.value = value;
    queued.receivedMs = millis();
    ++device->eventCount_;
  }

  static MakerBnoSpi* instance(sh2_Hal_t* self) {
    return active_ && self == &active_->_HAL ? active_ : nullptr;
  }

  bool waitForInterrupt(uint32_t timeoutMs) const {
    const uint32_t started = millis();
    while (digitalRead(intPin_) != LOW) {
      if (static_cast<uint32_t>(millis() - started) >= timeoutMs) return false;
      delay(1);
    }
    return true;
  }

  static uint32_t timeTransport(sh2_Hal_t*) { return micros(); }

  static int openTransport(sh2_Hal_t* self) {
    MakerBnoSpi* device = instance(self);
    if (!device) return -1;
    // Reaching this callback proves SHTP allocated its sole slot, even on error.
    device->sessionAllocated_ = true;
    digitalWrite(device->wakePin_, HIGH);
    device->startupReady_ = device->waitForInterrupt(STARTUP_TIMEOUT_MS);
    return device->startupReady_ ? 0 : -1;
  }

  // One SHTP exchange owns CS continuously. INT may deassert as soon as CS
  // falls; finishing the transfer avoids a second interrupt timeout/reset path.
  void exchange(const uint8_t* tx, unsigned txLen, uint32_t receivedUs) {
    memset(txScratch_, 0, sizeof(txScratch_));
    if (txLen) memcpy(txScratch_, tx, txLen);
    spi_->beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE3));
    digitalWrite(csPin_, LOW);
    spi_->transferBytes(txScratch_, rxScratch_, 4);
    const unsigned rawLength = static_cast<unsigned>(rxScratch_[0]) |
                               (static_cast<unsigned>(rxScratch_[1]) << 8);
    const unsigned rxLen = rawLength & 0x7FFFU;
    const bool validRx = rxLen >= 4 && rxLen <= SH2_HAL_MAX_TRANSFER_IN;
    if (rxLen == 0) ++diagnostics_.nullHeaders;
    else if (!validRx) ++diagnostics_.badHeaders;
    // Never clock an untrusted declared size. Finish only the known TX length
    // on invalid RX; a later transaction can start from the sensor's next header.
    unsigned total = txLen > 4 ? txLen : 4;
    if (validRx && rxLen > total) total = rxLen;
    if (total > 4) spi_->transferBytes(txScratch_ + 4, rxScratch_ + 4, total - 4);
    digitalWrite(csPin_, HIGH);
    spi_->endTransaction();
    if (txLen) ++diagnostics_.txPackets;
    if (validRx) {
      // Callers reserve space before starting a transfer, including writes.
      Packet& packet = rxQueue_[(rxHead_ + rxCount_) % RX_QUEUE_CAPACITY];
      packet.receivedUs = receivedUs;
      packet.len = rxLen;
      memcpy(packet.data, rxScratch_, rxLen);
      ++rxCount_;
      ++diagnostics_.rxPackets;
    }
  }

  int popPacket(uint8_t* buffer, unsigned len, uint32_t* timestampUs) {
    if (!rxCount_) return 0;
    const Packet& packet = rxQueue_[rxHead_];
    int result = 0;
    if (packet.len <= len) {
      memcpy(buffer, packet.data, packet.len);
      if (timestampUs) *timestampUs = packet.receivedUs;
      result = static_cast<int>(packet.len);
    } else {
      ++diagnostics_.rxBufferTooSmall;
    }
    rxHead_ = (rxHead_ + 1) % RX_QUEUE_CAPACITY;
    --rxCount_;
    return result;
  }

  static int readTransport(sh2_Hal_t* self, uint8_t* buffer, unsigned len,
                           uint32_t* timestampUs) {
    MakerBnoSpi* device = instance(self);
    if (!device || !device->startupReady_ || !device->spi_ || !buffer || len < 4)
      return 0;
    if (device->rxCount_) return device->popPacket(buffer, len, timestampUs);
    digitalWrite(device->wakePin_, HIGH);
    if (digitalRead(device->intPin_) != LOW) return 0;
    device->exchange(nullptr, 0, micros());
    return device->popPacket(buffer, len, timestampUs);
  }

  static int writeTransport(sh2_Hal_t* self, uint8_t* buffer, unsigned len) {
    MakerBnoSpi* device = instance(self);
    if (!device || !device->startupReady_ || !device->spi_ || !buffer || len < 4 ||
        len > SH2_HAL_MAX_TRANSFER_OUT) return -1;
    // SHTP services RX between write retries. Refuse without touching the bus so
    // that a full-duplex response cannot overwrite one of its queued packets.
    if (device->rxCount_ == RX_QUEUE_CAPACITY) {
      ++device->diagnostics_.rxQueueFull;
      return 0;
    }
    digitalWrite(device->wakePin_, LOW);
    delayMicroseconds(50);
    int accepted = -1;
    if (device->waitForInterrupt(WAKE_TIMEOUT_MS)) {
      device->exchange(buffer, len, micros());
      accepted = static_cast<int>(len);
    } else {
      ++device->diagnostics_.wakeTimeouts;
    }
    digitalWrite(device->wakePin_, HIGH);
    // Negative on timeout: SHTP retries zero forever, so zero is only backpressure.
    return accepted;
  }
};
