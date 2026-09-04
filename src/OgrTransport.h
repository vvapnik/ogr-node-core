// I2C-slave transport abstraction. A HAL package implements IOgrTransport on top of
// whatever the target offers (AVR TWI/USI, ESP-IDF I2C slave, a bit-banged driver, ...).
//
// Design note: the write-side of the protocol never needs the transport to NACK a
// specific byte (spec §5.3 — the node always ACKs and instead reports a bad PEC via
// STATUS). That means any transport that can hand the core a *buffered* received
// frame and a place to write a buffered response is sufficient — there is no
// dependency on per-byte ACK/NACK control anywhere in this interface.
#pragma once

#include <stdint.h>

namespace ogr {

class IOgrProtocolSink {
public:
  virtual ~IOgrProtocolSink() = default;

  // A full write transaction completed (STOP received after the data phase).
  // `data` is REG_ADDR, DATA_0..DATA_N, PEC exactly as they appeared on the bus.
  virtual void onI2cWrite(const uint8_t *data, uint8_t len) = 0;

  // The master began the read phase of a transaction for register `reg` (the
  // register byte captured during this transaction's write-phase prelude).
  // Implementations must fill `outBuf` with DATA_0..DATA_N followed by the PEC
  // byte and return the number of bytes written (<= maxOutLen).
  virtual uint8_t onI2cRead(uint8_t reg, uint8_t *outBuf, uint8_t maxOutLen) = 0;
};

class IOgrTransport {
public:
  virtual ~IOgrTransport() = default;

  // Bind the sink and start answering on `address`. Called once at boot with
  // ogr::kDefaultAddress, after EN_IN has been observed HIGH.
  virtual void begin(IOgrProtocolSink &sink, uint8_t address) = 0;

  // Re-programs the listening address. Called once, immediately after a SET_ADDR
  // write has been applied, and again (back to kDefaultAddress) is not needed —
  // on RESTART the node simply stops listening (see IOgrTransport::end()).
  virtual void setAddress(uint8_t address) = 0;

  // Stops answering on the bus entirely. Called on RESTART, before the node
  // re-enters the EN_IN wait state and calls begin() again.
  virtual void end() = 0;

  // Called frequently from the sketch's loop(). Transports that are fully
  // interrupt/hardware-driven (typical TWI/ESP-IDF peripherals) can leave this
  // empty; bit-banged transports (e.g. AVR USI) use it to do work that can't
  // safely happen inside an ISR.
  virtual void poll() = 0;
};

} // namespace ogr
