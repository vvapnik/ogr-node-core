// Non-volatile PLANT_UID persistence — spec §4.1. Mandatory for anchor modules,
// optional (may be a no-op stub) for everything else.
#pragma once

#include <stdint.h>

namespace ogr {

class IOgrStorage {
public:
  virtual ~IOgrStorage() = default;

  // Returns false if no UID was ever stored (core then reports kNoPlantUid).
  virtual bool loadPlantUid(uint32_t &uid) = 0;

  virtual void savePlantUid(uint32_t uid) = 0;
};

// Stub for non-anchor modules that don't support UID storage: reads always report
// kNoPlantUid, writes are silently ignored — exactly the behavior spec §4.1 requires
// of modules with IS_ANCHOR == 0 that opt out of PLANT_UID support.
class NullStorage : public IOgrStorage {
public:
  bool loadPlantUid(uint32_t & /*uid*/) override { return false; }
  void savePlantUid(uint32_t /*uid*/) override {}
};

} // namespace ogr
