// Minimal mocks so the core protocol engine can be exercised on the host,
// with no real hardware or platform SDK involved.
#pragma once

#include "OgrGpio.h"
#include "OgrStorage.h"
#include "OgrTransport.h"
#include <cstring>

namespace ogr_test {

class MockGpio : public ogr::IOgrGpio {
public:
  void begin() override { beginCalls++; }
  bool readEnIn() override { return enIn; }
  void writeEnOut(bool high) override { enOut = high; }

  bool enIn = false;
  bool enOut = false;
  int beginCalls = 0;
};

class MockStorage : public ogr::IOgrStorage {
public:
  bool loadPlantUid(uint32_t &uid) override {
    if (!hasStored) return false;
    uid = stored;
    return true;
  }
  void savePlantUid(uint32_t uid) override {
    stored = uid;
    hasStored = true;
    saveCount++;
  }

  bool hasStored = false;
  uint32_t stored = 0;
  int saveCount = 0;
};

// Drives the sink exactly the way a real transport would: begin()/setAddress()
// just record state, and the test calls write()/read() directly to simulate
// bus transactions instead of going through interrupt plumbing.
class MockTransport : public ogr::IOgrTransport {
public:
  void begin(ogr::IOgrProtocolSink &sink, uint8_t address) override {
    sink_ = &sink;
    listening = true;
    currentAddress = address;
    beginCalls++;
  }
  void setAddress(uint8_t address) override { currentAddress = address; }
  void end() override { listening = false; }
  void poll() override { pollCalls++; }

  // Test helpers, mirroring what a real HAL's ISR would do.
  void write(const uint8_t *data, uint8_t len) { sink_->onI2cWrite(data, len); }
  uint8_t read(uint8_t reg, uint8_t *outBuf, uint8_t maxLen) { return sink_->onI2cRead(reg, outBuf, maxLen); }

  ogr::IOgrProtocolSink *sink_ = nullptr;
  bool listening = false;
  uint8_t currentAddress = 0;
  int beginCalls = 0;
  int pollCalls = 0;
};

} // namespace ogr_test
