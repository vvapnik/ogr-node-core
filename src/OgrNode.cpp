#include "OgrNode.h"

namespace ogr {

namespace {
constexpr uint8_t regByte(Reg r) { return static_cast<uint8_t>(r); }
} // namespace

OgrNode::OgrNode(ClassId classId, bool isAnchor, bool hasBlueprint, uint32_t blueprintUid)
    : classId_(classId), isAnchor_(isAnchor), hasBlueprint_(hasBlueprint),
      blueprintUid_(hasBlueprint ? blueprintUid : kNoBlueprintUid) {}

int OgrNode::addEndpoint(TypeId type, Mode mode, Format format, ReadCallback onRead,
                          WriteCallback onWrite, void *ctx) {
  if (endpointCount_ >= OGR_MAX_ENDPOINTS) {
    return -1;
  }
  EndpointDef &ep = endpoints_[endpointCount_];
  ep.type = type;
  ep.mode = mode;
  ep.format = format;
  ep.onRead = onRead;
  ep.onWrite = onWrite;
  ep.ctx = ctx;
  return endpointCount_++;
}

void OgrNode::begin(IOgrTransport &transport, IOgrGpio &gpio, IOgrStorage &storage) {
  transport_ = &transport;
  gpio_ = &gpio;
  storage_ = &storage;
  plantUidSupported_ = true;

  uint32_t loaded = kNoPlantUid;
  plantUid_ = storage_->loadPlantUid(loaded) ? loaded : kNoPlantUid;
  plantUidDirty_ = false;

  gpio_->begin();
  gpio_->writeEnOut(false);
  lifecycle_ = Lifecycle::WaitingForEnIn;
  address_ = 0;
  status_ = Status::Ok;
}

void OgrNode::begin(IOgrTransport &transport, IOgrGpio &gpio) {
  transport_ = &transport;
  gpio_ = &gpio;
  storage_ = &nullStorage_;
  plantUidSupported_ = false;
  plantUid_ = kNoPlantUid;
  plantUidDirty_ = false;

  gpio_->begin();
  gpio_->writeEnOut(false);
  lifecycle_ = Lifecycle::WaitingForEnIn;
  address_ = 0;
  status_ = Status::Ok;
}

void OgrNode::update() {
  if (!transport_) {
    return;
  }

  // Must run before anything else touches the transport: see the member
  // comments in OgrNode.h for why these can't happen synchronously inside
  // onI2cWrite() (the ISR call path for SET_ADDR/RESTART).
  if (transportAddressPending_) {
    transportAddressPending_ = false;
    transport_->setAddress(pendingTransportAddress_);
  }
  if (transportEndPending_) {
    transportEndPending_ = false;
    transport_->end();
  }

  if (lifecycle_ == Lifecycle::WaitingForEnIn) {
    if (gpio_->readEnIn()) {
      address_ = kDefaultAddress;
      epSelect_ = 0;
      lifecycle_ = Lifecycle::Unassigned;
      transport_->begin(*this, kDefaultAddress);
    }
  }

  if (plantUidDirty_ && plantUidSupported_) {
    storage_->savePlantUid(plantUid_);
    plantUidDirty_ = false;
  }

  transport_->poll();
}

uint8_t OgrNode::writePec(uint8_t reg, const uint8_t *data, uint8_t len) const {
  Crc8Builder b;
  b.add(static_cast<uint8_t>(address_ << 1)); // NODE_ADDR+W
  b.add(reg);
  b.add(data, len);
  return b.value();
}

uint8_t OgrNode::readPec(uint8_t reg, const uint8_t *data, uint8_t len) const {
  Crc8Builder b;
  b.add(static_cast<uint8_t>(address_ << 1));      // NODE_ADDR+W
  b.add(reg);
  b.add(static_cast<uint8_t>((address_ << 1) | 1)); // NODE_ADDR+R
  b.add(data, len);
  return b.value();
}

void OgrNode::setPlantUidByte(uint8_t byteIndex, uint8_t value) {
  uint32_t mask = ~(static_cast<uint32_t>(0xFFu) << (byteIndex * 8));
  plantUid_ = (plantUid_ & mask) | (static_cast<uint32_t>(value) << (byteIndex * 8));
  plantUidDirty_ = true;
  if (plantUidCb_) {
    plantUidCb_(plantUid_);
  }
}

void OgrNode::doRestart(RestartType type) {
  if (restartCb_) {
    restartCb_(type);
  }
  gpio_->writeEnOut(false);
  transportEndPending_ = true; // transport_->end() deferred to update() — see OgrNode.h
  lifecycle_ = Lifecycle::WaitingForEnIn;
  address_ = 0;
  epSelect_ = 0;
  status_ = Status::Ok;
}

void OgrNode::applySetAddr(uint8_t newAddr) {
  address_ = newAddr;
  pendingTransportAddress_ = newAddr;
  transportAddressPending_ = true; // transport_->setAddress() deferred to update() — see OgrNode.h
  gpio_->writeEnOut(true);
  lifecycle_ = Lifecycle::Assigned;
  epSelect_ = 0;
  status_ = Status::Ok;
  if (enumeratedCb_) {
    enumeratedCb_(newAddr);
  }
}

void OgrNode::onI2cWrite(const uint8_t *data, uint8_t len) {
  if (len < 2) {
    return; // malformed: need at least REG_ADDR + PEC
  }

  uint8_t reg = data[0];
  const uint8_t *payload = data + 1;
  uint8_t payloadLen = static_cast<uint8_t>(len - 2);
  uint8_t receivedPec = data[len - 1];

  // Write validation — spec §5.3: always ACKed at the transport level (see
  // IOgrTransport); a bad PEC is discovered here instead and surfaced via
  // STATUS rather than an in-band NACK.
  if (writePec(reg, payload, payloadLen) != receivedPec) {
    status_ = Status::Error;
    return;
  }

  if (reg == regByte(Reg::SetAddr)) {
    if (payloadLen == 1 && lifecycle_ == Lifecycle::Unassigned) {
      applySetAddr(payload[0]);
    }
  } else if (reg == regByte(Reg::Restart)) {
    if (payloadLen == 1) {
      doRestart(static_cast<RestartType>(payload[0]));
    }
  } else if (reg >= regByte(Reg::PlantUid0) && reg <= regByte(Reg::PlantUid3)) {
    if (payloadLen == 1 && plantUidSupported_) {
      setPlantUidByte(static_cast<uint8_t>(reg - regByte(Reg::PlantUid0)), payload[0]);
    }
  } else if (reg == regByte(Reg::EpSelect)) {
    if (payloadLen == 1) {
      if (payload[0] < endpointCount_) {
        epSelect_ = payload[0];
      } else {
        status_ = Status::Error; // spec §4.2: invalid index — No-Op + Error
      }
    }
  } else if (reg == regByte(Reg::EpData)) {
    if (payloadLen == kEpDataWidth && epSelect_ < endpointCount_) {
      EndpointDef &ep = endpoints_[epSelect_];
      bool writable = (static_cast<uint8_t>(ep.mode) & static_cast<uint8_t>(Mode::Write)) != 0;
      if (writable && ep.onWrite) {
        uint32_t value = static_cast<uint32_t>(payload[0]) | (static_cast<uint32_t>(payload[1]) << 8) |
                          (static_cast<uint32_t>(payload[2]) << 16) | (static_cast<uint32_t>(payload[3]) << 24);
        ep.onWrite(ep.ctx, value);
      }
    }
  }
  // Any other register (read-only, or unrecognized): no-op. Not an error
  // condition the spec defines, so STATUS is left untouched.
}

uint8_t OgrNode::onI2cRead(uint8_t reg, uint8_t *outBuf, uint8_t maxOutLen) {
  uint8_t data[kEpDataWidth] = {0, 0, 0, 0};
  uint8_t dataLen = 0;

  if (reg == regByte(Reg::ProtVer)) {
    data[0] = kProtocolVersion;
    dataLen = 1;
  } else if (reg == regByte(Reg::Status)) {
    data[0] = static_cast<uint8_t>(status_);
    dataLen = 1;
  } else if (reg == regByte(Reg::Class)) {
    data[0] = static_cast<uint8_t>(classId_);
    dataLen = 1;
  } else if (reg == regByte(Reg::EpCount)) {
    data[0] = endpointCount_;
    dataLen = 1;
  } else if (reg == regByte(Reg::SysFlags)) {
    data[0] = static_cast<uint8_t>((isAnchor_ ? kFlagIsAnchor : 0) | (hasBlueprint_ ? kFlagHasBlueprint : 0));
    dataLen = 1;
  } else if (reg >= regByte(Reg::PlantUid0) && reg <= regByte(Reg::PlantUid3)) {
    data[0] = plantUidByte(static_cast<uint8_t>(reg - regByte(Reg::PlantUid0)));
    dataLen = 1;
  } else if (reg >= regByte(Reg::BpUid0) && reg <= regByte(Reg::BpUid3)) {
    data[0] = blueprintUidByte(static_cast<uint8_t>(reg - regByte(Reg::BpUid0)));
    dataLen = 1;
  } else if (reg == regByte(Reg::EpSelect)) {
    data[0] = epSelect_;
    dataLen = 1;
  } else if (reg == regByte(Reg::EpDescriptor)) {
    if (epSelect_ < endpointCount_) {
      const EndpointDef &ep = endpoints_[epSelect_];
      data[0] = static_cast<uint8_t>(ep.type);
      data[1] = static_cast<uint8_t>(ep.mode);
      data[2] = static_cast<uint8_t>(ep.format);
    }
    dataLen = kEpDescriptorWidth;
  } else if (reg == regByte(Reg::EpData)) {
    if (epSelect_ < endpointCount_) {
      const EndpointDef &ep = endpoints_[epSelect_];
      bool readable = (static_cast<uint8_t>(ep.mode) & static_cast<uint8_t>(Mode::Read)) != 0;
      if (readable && ep.onRead) {
        uint32_t value = ep.onRead(ep.ctx);
        data[0] = static_cast<uint8_t>(value);
        data[1] = static_cast<uint8_t>(value >> 8);
        data[2] = static_cast<uint8_t>(value >> 16);
        data[3] = static_cast<uint8_t>(value >> 24);
      }
    }
    dataLen = kEpDataWidth;
  } else {
    dataLen = 1; // unrecognized register: harmless single zero byte + PEC
  }

  uint8_t pec = readPec(reg, data, dataLen);

  uint8_t total = dataLen;
  if (static_cast<uint16_t>(total) + 1 > maxOutLen) {
    total = maxOutLen > 0 ? static_cast<uint8_t>(maxOutLen - 1) : 0;
  }
  for (uint8_t i = 0; i < total; ++i) {
    outBuf[i] = data[i];
  }
  outBuf[total] = pec;
  return static_cast<uint8_t>(total + 1);
}

} // namespace ogr
