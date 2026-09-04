// EN_IN / EN_OUT line abstraction — spec §3 (Enumeration & Addressing).
//
// Timing on these two lines is not latency-critical (the master polls at its own
// pace, and a HIGH level only needs to be observed once per boot/RESTART), so a
// HAL only needs to support plain polled digital I/O here — no interrupts required.
#pragma once

namespace ogr {

class IOgrGpio {
public:
  virtual ~IOgrGpio() = default;

  // One-time setup, called from OgrNode::begin().
  virtual void begin() = 0;

  // EN_IN (bottom connector, input). HIGH means "this module may register".
  virtual bool readEnIn() = 0;

  // EN_OUT (top connector, output). Drives EN_IN of whatever is stacked above.
  virtual void writeEnOut(bool high) = 0;
};

} // namespace ogr
