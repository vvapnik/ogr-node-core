// CRC-8/SMBUS (poly 0x07, init 0x00, no reflection, no final XOR) — spec §5.3.
#pragma once

#include <stdint.h>

namespace ogr {

class Crc8 {
public:
  static uint8_t update(uint8_t crc, uint8_t byte) {
    crc ^= byte;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) : static_cast<uint8_t>(crc << 1);
    }
    return crc;
  }

  static uint8_t compute(const uint8_t *data, uint8_t len, uint8_t seed = 0x00) {
    uint8_t crc = seed;
    for (uint8_t i = 0; i < len; ++i) {
      crc = update(crc, data[i]);
    }
    return crc;
  }
};

// Small running-CRC accumulator so callers can feed address-phase bytes and a
// data buffer without assembling them into one contiguous array first.
class Crc8Builder {
public:
  void add(uint8_t byte) { crc_ = Crc8::update(crc_, byte); }

  void add(const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len; ++i) {
      add(data[i]);
    }
  }

  uint8_t value() const { return crc_; }

private:
  uint8_t crc_ = 0x00;
};

} // namespace ogr
