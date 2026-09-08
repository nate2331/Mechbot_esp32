#pragma once

#include <cstdint>

// Passive BNO085 UART-RVC parser. Angles remain in hundredths of a degree;
// acceleration remains in mg. No timing, allocation or Arduino APIs are needed.
class RvcParser {
 public:
  struct Sample {
    uint8_t index = 0;
    int16_t yaw = 0;
    int16_t pitch = 0;
    int16_t roll = 0;
    int16_t ax = 0;
    int16_t ay = 0;
    int16_t az = 0;
  };

  bool feed(uint8_t byte) {
    if (used_ == 0) {
      if (byte == HEADER) buffer_[used_++] = byte;
      return false;
    }
    if (used_ == 1) {
      if (byte == HEADER) buffer_[used_++] = byte;
      else used_ = 0;
      return false;
    }

    buffer_[used_++] = byte;
    if (used_ < FRAME_SIZE) return false;

    uint16_t checksum = 0;
    for (uint8_t i = 2; i < FRAME_SIZE - 1; ++i) checksum += buffer_[i];
    if (static_cast<uint8_t>(checksum) != buffer_[FRAME_SIZE - 1]) {
      increment(badChecksums_);
      resynchronize();
      return false;
    }

    const uint8_t index = buffer_[2];
    if (haveSample_ && static_cast<uint8_t>(index - sample_.index) != 1)
      increment(indexDiscontinuities_);
    sample_.index = index;
    sample_.yaw = signedLittleEndian(3);
    sample_.pitch = signedLittleEndian(5);
    sample_.roll = signedLittleEndian(7);
    sample_.ax = signedLittleEndian(9);
    sample_.ay = signedLittleEndian(11);
    sample_.az = signedLittleEndian(13);
    haveSample_ = true;
    increment(validFrames_);
    used_ = 0;
    return true;
  }

  const Sample& sample() const { return sample_; }
  uint32_t validFrames() const { return validFrames_; }
  uint32_t badChecksums() const { return badChecksums_; }
  uint32_t indexDiscontinuities() const { return indexDiscontinuities_; }

 private:
  static constexpr uint8_t HEADER = 0xAA;
  static constexpr uint8_t FRAME_SIZE = 19;
  uint8_t buffer_[FRAME_SIZE] = {};
  uint8_t used_ = 0;
  Sample sample_;
  bool haveSample_ = false;
  uint32_t validFrames_ = 0;
  uint32_t badChecksums_ = 0;
  uint32_t indexDiscontinuities_ = 0;

  static void increment(uint32_t& counter) {
    if (counter != UINT32_MAX) ++counter;
  }

  int16_t signedLittleEndian(uint8_t offset) const {
    const uint16_t raw = static_cast<uint16_t>(buffer_[offset]) |
        (static_cast<uint16_t>(buffer_[offset + 1]) << 8);
    // Keep conversion defined even on implementations where converting an
    // out-of-range unsigned value directly to int16_t is implementation-defined.
    const int32_t value = raw >= 0x8000U ? static_cast<int32_t>(raw) - 65536
                                       : static_cast<int32_t>(raw);
    return static_cast<int16_t>(value);
  }

  void resynchronize() {
    // A failed candidate may contain the next real header. Retain its suffix,
    // including overlapping AA AA AA sequences, rather than dropping19 bytes.
    for (uint8_t start = 1; start + 1 < FRAME_SIZE; ++start) {
      if (buffer_[start] == HEADER && buffer_[start + 1] == HEADER) {
        const uint8_t remaining = FRAME_SIZE - start;
        for (uint8_t i = 0; i < remaining; ++i) buffer_[i] = buffer_[start + i];
        used_ = remaining;
        return;
      }
    }
    if (buffer_[FRAME_SIZE - 1] == HEADER) {
      buffer_[0] = HEADER;
      used_ = 1;
    } else {
      used_ = 0;
    }
  }
};
