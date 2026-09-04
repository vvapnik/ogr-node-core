// OpenGardenRack protocol constants — mirrors docs/04-06-07 of the spec.
// https://github.com/vvapnik/open-garden-rack
#pragma once

#include <stdint.h>

namespace ogr {

// ---- Protocol-fixed addresses -------------------------------------------

constexpr uint8_t kDefaultAddress = 0x08;
constexpr uint8_t kProtocolVersion = 0x10; // v1.0

// ---- System Block registers (0x00-0x1F) — spec §4.1 ---------------------

enum class Reg : uint8_t {
  ProtVer = 0x00,
  Status = 0x01,
  Class = 0x02,
  EpCount = 0x03,
  SysFlags = 0x04,
  SetAddr = 0x05,
  Restart = 0x06,
  PlantUid0 = 0x07,
  PlantUid1 = 0x08,
  PlantUid2 = 0x09,
  PlantUid3 = 0x0A,
  BpUid0 = 0x0B,
  BpUid1 = 0x0C,
  BpUid2 = 0x0D,
  BpUid3 = 0x0E,
  EpSelect = 0x0F,
  // 0x10-0x1F reserved

  // Capability descriptor window: ONE register: select 0x20, read exactly 3
  // bytes (Type, Mode, Format) in a single transaction — these are not three
  // independently-addressable registers.
  EpDescriptor = 0x20,

  // Data window: ONE register: select 0x40, read/write exactly 4 bytes in a
  // single transaction — same non-independent-registers note as above.
  EpData = 0x40,
};

// Fixed widths (bytes, excluding PEC) of the two multi-byte register windows.
constexpr uint8_t kEpDescriptorWidth = 3;
constexpr uint8_t kEpDataWidth = 4;

enum class Status : uint8_t {
  Ok = 0,
  Error = 1,
  Busy = 2,
};

enum class RestartType : uint8_t {
  Soft = 0x01,
  Hard = 0x02,
};

// SYS_FLAGS bits (0x04)
constexpr uint8_t kFlagIsAnchor = 1u << 0;
constexpr uint8_t kFlagHasBlueprint = 1u << 1;

constexpr uint32_t kNoPlantUid = 0xFFFFFFFFu;
constexpr uint32_t kNoBlueprintUid = 0x00000000u;

// Largest frame either direction ever needs to carry — spec §5.2: at most
// REG_ADDR + 4 data bytes + PEC on write, 4 data bytes + PEC on read.
constexpr uint8_t kMaxWriteFrame = 1 + 4 + 1;
constexpr uint8_t kMaxReadFrame = 4 + 1;

// ---- Standard CLASS IDs — spec §6 ----------------------------------------

enum class ClassId : uint8_t {
  PlantContainer = 0x01,
  Light = 0x02,
  WaterSupply = 0x03,
  Climate = 0x04,
  Custom = 0xFF,
};

// ---- Standard Type IDs — spec §7 -----------------------------------------

enum class TypeId : uint8_t {
  // Sensors (0x40-0x7F)
  Moisture = 0x40,
  Temp = 0x41,
  Humidity = 0x42,
  Light = 0x43,
  Weight = 0x44,
  Distance = 0x45,
  WaterLevel = 0x46,

  // Actuators (0x80-0xBF)
  Relay = 0x80,
  Dimmer = 0x81,
  WaterValve = 0x82,
  LightSwitch = 0x83,
  WaterPump = 0x84,
  Vibro = 0x85,

  // Widgets & external devices (0xC0-0xFF)
  Camera = 0xC0,
};

// Endpoint access mode (capability descriptor byte 1, 0x21)
enum class Mode : uint8_t {
  Read = 0x01,
  Write = 0x02,
  ReadWrite = 0x03,
};

// Endpoint data format (capability descriptor byte 2, 0x22)
enum class Format : uint8_t {
  Binary = 0x01,
  Percent = 0x05,
  Raw16 = 0x0A,
};

} // namespace ogr
