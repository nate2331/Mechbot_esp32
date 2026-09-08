#pragma once

#include <Adafruit_BNO08x.h>

// The Adafruit SH-2 transport has process-global state: use one BNO08x instance.
// Keep its sensor decoder and SPI framing, adding the STRDC PS0/WAKE handshake.
class MakerBnoSpi : public Adafruit_BNO08x {
 public:
  MakerBnoSpi(int8_t resetPin, uint8_t wakePin, uint8_t intPin)
      : Adafruit_BNO08x(resetPin), wakePin_(wakePin), intPin_(intPin) {}

  bool begin_SPI(uint8_t csPin, uint8_t intPin, SPIClass* spi = &SPI,
                 int32_t sensorId = 0) {
    if (intPin != intPin_ || !spi) return false;
    // Release the previous SHTP slot before Adafruit replaces its device/HAL.
    // sh2_close() itself is not safe when sh2_open() never allocated a slot.
    closeSession();
    spi_ = spi;
    csPin_ = csPin;
    return Adafruit_BNO08x::begin_SPI(csPin, intPin, spi, sensorId);
  }

  MakerBnoSpi(const MakerBnoSpi&) = delete;
  MakerBnoSpi& operator=(const MakerBnoSpi&) = delete;

 protected:
  bool _init(int32_t sensorId) override {
    pinMode(wakePin_, OUTPUT);
    digitalWrite(wakePin_, HIGH);  // SPI mode must be selected before reset.
    originalRead_ = _HAL.read;
    active_ = this;
    _HAL.open = openTransport;
    _HAL.read = readTransport;
    _HAL.write = writeTransport;
    // The base initializer resets the sensor, then invokes our HAL open.
    // SHTP ignores open's return code and SH-2 may continue its own timeouts;
    // the 500 ms startup wait does not bound the entire base initialization.
    const bool initialized = Adafruit_BNO08x::_init(sensorId);
    const bool ready = initialized && startupReady_;
    if (!ready) closeSession();
    return ready;
  }

 private:
  static constexpr uint32_t STARTUP_TIMEOUT_MS = 500;
  static constexpr uint32_t WAKE_TIMEOUT_MS = 50;
  static constexpr uint32_t SPI_CLOCK_HZ = 1000000;
  uint8_t wakePin_;
  uint8_t intPin_;
  uint8_t csPin_ = 0;
  SPIClass* spi_ = nullptr;
  bool sessionAllocated_ = false;
  bool startupReady_ = false;
  int (*originalRead_)(sh2_Hal_t*, uint8_t*, unsigned, uint32_t*) = nullptr;
  inline static MakerBnoSpi* active_ = nullptr;

  void closeSession() {
    if (sessionAllocated_) {
      sh2_close();
      sessionAllocated_ = false;
    }
    startupReady_ = false;
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

  static int openTransport(sh2_Hal_t* self) {
    MakerBnoSpi* device = instance(self);
    if (!device) return -1;
    // SHTP stores the HAL in its sole slot before invoking open, even when
    // open fails. Reaching here proves that guarded sh2_close() will be safe.
    device->sessionAllocated_ = true;
    // Hold SPI mode high until the first sensor-ready assertion. Unlike the
    // native open callback, a timeout does not silently issue another reset.
    digitalWrite(device->wakePin_, HIGH);
    device->startupReady_ = device->waitForInterrupt(STARTUP_TIMEOUT_MS);
    return device->startupReady_ ? 0 : -1;
  }

  static int readTransport(sh2_Hal_t* self, uint8_t* buffer, unsigned len,
                           uint32_t* timestampUs) {
    MakerBnoSpi* device = instance(self);
    if (!device || !device->startupReady_ || !device->originalRead_ ||
        !buffer || len < 4) return 0;
    // Ordinary no-data polling must not enter the native 500 ms timeout/reset.
    // Keep WAKE high if the native header/payload transfer later resets on error.
    digitalWrite(device->wakePin_, HIGH);
    if (digitalRead(device->intPin_) != LOW) return 0;
    if (timestampUs) *timestampUs = micros();
    return device->originalRead_(self, buffer, len, timestampUs);
  }

  static int writeTransport(sh2_Hal_t* self, uint8_t* buffer, unsigned len) {
    MakerBnoSpi* device = instance(self);
    if (!device || !device->startupReady_ || !device->spi_ || !buffer || len == 0 ||
        len > SH2_HAL_MAX_TRANSFER_OUT) return 0;
    digitalWrite(device->wakePin_, LOW);
    delayMicroseconds(50);
    int accepted = 0;
    if (device->waitForInterrupt(WAKE_TIMEOUT_MS)) {
      // Native write can reset on a second INT wait. Transfer directly so a
      // timeout can never reset the sensor with WAKE low (UART boot mode).
      device->spi_->beginTransaction(SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE3));
      digitalWrite(device->csPin_, LOW);
      device->spi_->writeBytes(buffer, len);
      digitalWrite(device->csPin_, HIGH);
      device->spi_->endTransaction();
      accepted = static_cast<int>(len);
    }
    digitalWrite(device->wakePin_, HIGH);
    return accepted;
  }
};
