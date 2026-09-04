// OpenGardenRack node (slave) protocol engine — platform-independent.
//
// Usage sketch:
//
//   OgrNode node(ogr::ClassId::PlantContainer, /*isAnchor=*/true);
//
//   void setup() {
//     int moisture = node.addEndpoint(ogr::TypeId::Moisture, ogr::Mode::Read,
//                                      ogr::Format::Raw16,
//                                      [](void *) -> uint32_t { return analogRead(A0); });
//     node.begin(myTransport, myGpio, myStorage);
//   }
//
//   void loop() { node.update(); }
//
// A HAL package (ogr-node-hal-*) supplies the IOgrTransport/IOgrGpio/IOgrStorage
// implementations; this class never touches hardware directly.
#pragma once

#include <stdint.h>
#include "OgrCrc8.h"
#include "OgrGpio.h"
#include "OgrStorage.h"
#include "OgrTransport.h"
#include "OgrTypes.h"

#ifndef OGR_MAX_ENDPOINTS
#define OGR_MAX_ENDPOINTS 8
#endif

namespace ogr {

// ctx is whatever was passed to addEndpoint() — lets one callback serve
// several endpoints (e.g. distinguished by a pin number stashed in ctx).
using ReadCallback = uint32_t (*)(void *ctx);
using WriteCallback = void (*)(void *ctx, uint32_t value);

using RestartCallback = void (*)(RestartType type);
using PlantUidCallback = void (*)(uint32_t uid);
using EnumeratedCallback = void (*)(uint8_t address);

struct EndpointDef {
  TypeId type = TypeId::Relay;
  Mode mode = Mode::Read;
  Format format = Format::Binary;
  ReadCallback onRead = nullptr;
  WriteCallback onWrite = nullptr;
  void *ctx = nullptr;
};

class OgrNode : public IOgrProtocolSink {
public:
  OgrNode(ClassId classId, bool isAnchor, bool hasBlueprint = false,
          uint32_t blueprintUid = kNoBlueprintUid);

  // Declares one endpoint. Order of calls fixes EP_SELECT indices (first call
  // is endpoint 0). Returns the assigned index, or -1 if OGR_MAX_ENDPOINTS is
  // already used up. Call during setup(), before begin().
  int addEndpoint(TypeId type, Mode mode, Format format, ReadCallback onRead = nullptr,
                   WriteCallback onWrite = nullptr, void *ctx = nullptr);

  // Optional hooks. All are called synchronously off the transport (may be
  // ISR context on hardware-TWI/I2C-peripheral platforms) except where noted
  // — keep them fast, no blocking calls.
  void onRestart(RestartCallback cb) { restartCb_ = cb; }
  void onPlantUidChanged(PlantUidCallback cb) { plantUidCb_ = cb; }
  void onEnumerated(EnumeratedCallback cb) { enumeratedCb_ = cb; }

  // Anchor / UID-capable modules: PLANT_UID persists across RESTART and power
  // loss via `storage`. Required for IS_ANCHOR modules (spec §4.1).
  void begin(IOgrTransport &transport, IOgrGpio &gpio, IOgrStorage &storage);

  // Modules that don't support PLANT_UID at all: reads always report
  // kNoPlantUid, writes are silently ignored (spec §4.1, non-anchor modules).
  void begin(IOgrTransport &transport, IOgrGpio &gpio);

  // Call every loop() iteration. Drives the EN_IN wait state, flushes a
  // pending PLANT_UID save to storage (kept off the transport's call path
  // since a flash/EEPROM write can take milliseconds), and lets the
  // transport do any deferred work it needs (see IOgrTransport::poll()).
  void update();

  void setStatus(Status s) { status_ = s; }
  Status status() const { return status_; }

  // 0 while waiting on EN_IN or sitting at the default address; the assigned
  // 7-bit address once SET_ADDR has been applied.
  uint8_t address() const { return address_; }

  void setBlueprintUid(uint32_t uid) { blueprintUid_ = uid; }

  // IOgrProtocolSink — called by the transport, not meant to be called directly.
  void onI2cWrite(const uint8_t *data, uint8_t len) override;
  uint8_t onI2cRead(uint8_t reg, uint8_t *outBuf, uint8_t maxOutLen) override;

private:
  enum class Lifecycle : uint8_t { WaitingForEnIn, Unassigned, Assigned };

  uint8_t writePec(uint8_t reg, const uint8_t *data, uint8_t len) const;
  uint8_t readPec(uint8_t reg, const uint8_t *data, uint8_t len) const;
  void doRestart(RestartType type);
  void applySetAddr(uint8_t newAddr);
  void setPlantUidByte(uint8_t byteIndex, uint8_t value);
  uint8_t plantUidByte(uint8_t byteIndex) const { return static_cast<uint8_t>(plantUid_ >> (byteIndex * 8)); }
  uint8_t blueprintUidByte(uint8_t byteIndex) const { return static_cast<uint8_t>(blueprintUid_ >> (byteIndex * 8)); }

  // Fixed identity
  ClassId classId_;
  bool isAnchor_;
  bool hasBlueprint_;
  uint32_t blueprintUid_;

  // Endpoints
  EndpointDef endpoints_[OGR_MAX_ENDPOINTS];
  uint8_t endpointCount_ = 0;
  uint8_t epSelect_ = 0;

  // System state
  Status status_ = Status::Ok;
  uint8_t address_ = 0;
  Lifecycle lifecycle_ = Lifecycle::WaitingForEnIn;

  // PLANT_UID: always kept in RAM so a read never has to touch storage
  // synchronously; writes stage here and get flushed from update().
  uint32_t plantUid_ = kNoPlantUid;
  bool plantUidSupported_ = false;
  bool plantUidDirty_ = false;

  // Collaborators (not owned)
  IOgrTransport *transport_ = nullptr;
  IOgrGpio *gpio_ = nullptr;
  IOgrStorage *storage_ = nullptr;
  NullStorage nullStorage_;

  // User hooks
  RestartCallback restartCb_ = nullptr;
  PlantUidCallback plantUidCb_ = nullptr;
  EnumeratedCallback enumeratedCb_ = nullptr;
};

} // namespace ogr
