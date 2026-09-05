#include <unity.h>
#include "OgrNode.h"
#include "OgrCrc8.h"
#include "../mocks/MockHal.h"

using namespace ogr;
using namespace ogr_test;

namespace {

// Builds a write frame [REG, DATA_0..DATA_N-1, PEC] addressed to `addr`,
// mirroring spec §5.2/§5.3 exactly — independent of OgrNode's own PEC helpers
// so the test doesn't just trivially agree with itself.
void buildWriteFrame(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t dataLen, uint8_t *outFrame) {
  Crc8Builder b;
  b.add(static_cast<uint8_t>(addr << 1));
  b.add(reg);
  b.add(data, dataLen);

  outFrame[0] = reg;
  for (uint8_t i = 0; i < dataLen; ++i) outFrame[1 + i] = data[i];
  outFrame[1 + dataLen] = b.value();
}

uint8_t expectedReadPec(uint8_t addr, uint8_t reg, const uint8_t *data, uint8_t dataLen) {
  Crc8Builder b;
  b.add(static_cast<uint8_t>(addr << 1));
  b.add(reg);
  b.add(static_cast<uint8_t>((addr << 1) | 1));
  b.add(data, dataLen);
  return b.value();
}

MockGpio gpio;
MockStorage storage;
MockTransport transport;

uint8_t lastRestartType = 0;
int restartCalls = 0;
uint32_t lastPlantUid = 0xDEADBEEF;
int plantUidCalls = 0;
uint8_t lastEnumeratedAddr = 0;
int enumeratedCalls = 0;

void onRestart(RestartType t) {
  lastRestartType = static_cast<uint8_t>(t);
  restartCalls++;
}
void onPlantUidChanged(uint32_t uid) {
  lastPlantUid = uid;
  plantUidCalls++;
}
void onEnumerated(uint8_t addr) {
  lastEnumeratedAddr = addr;
  enumeratedCalls++;
}

uint32_t fixedMoistureReading = 0x1234;
uint32_t readMoisture(void *) { return fixedMoistureReading; }

uint32_t lastRelayValue = 0xFFFFFFFF;
int relayWriteCalls = 0;
void writeRelay(void *, uint32_t value) {
  lastRelayValue = value;
  relayWriteCalls++;
}

} // namespace

void setUp() {
  gpio = MockGpio();
  storage = MockStorage();
  transport = MockTransport();
  restartCalls = 0;
  plantUidCalls = 0;
  enumeratedCalls = 0;
  fixedMoistureReading = 0x1234;
  lastRelayValue = 0xFFFFFFFF;
  relayWriteCalls = 0;
}
void tearDown() {}

// A node stays silent until EN_IN goes high, then answers on the default
// address — spec §3.1 steps 1-3.
void test_waits_for_en_in_before_listening() {
  OgrNode node(ClassId::Custom, false);
  node.begin(transport, gpio, storage);

  node.update();
  TEST_ASSERT_FALSE(transport.listening);
  TEST_ASSERT_EQUAL_UINT8(0, node.address());

  gpio.enIn = true;
  node.update();
  TEST_ASSERT_TRUE(transport.listening);
  TEST_ASSERT_EQUAL_UINT8(kDefaultAddress, node.address());
}

// SET_ADDR moves the node off 0x08, raises EN_OUT, and fires onEnumerated —
// spec §3.1 step 3.
void test_set_addr_assigns_and_raises_en_out() {
  OgrNode node(ClassId::Custom, false);
  node.onEnumerated(onEnumerated);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();

  uint8_t data[] = {0x2A};
  uint8_t frame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), data, 1, frame);
  transport.write(frame, 3);

  TEST_ASSERT_EQUAL_UINT8(0x2A, node.address()); // updated immediately
  node.update(); // transport_->setAddress() is deferred to here — see OgrNode.h
  TEST_ASSERT_EQUAL_UINT8(0x2A, transport.currentAddress);
  TEST_ASSERT_TRUE(gpio.enOut);
  TEST_ASSERT_EQUAL_INT(1, enumeratedCalls);
  TEST_ASSERT_EQUAL_UINT8(0x2A, lastEnumeratedAddr);
}

// A write with a corrupted PEC is discarded and reported via STATUS, never
// via NACK (spec §5.3, revised).
void test_bad_pec_sets_status_error_and_is_discarded() {
  OgrNode node(ClassId::Custom, false);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(node.status()));

  uint8_t frame[] = {static_cast<uint8_t>(Reg::SetAddr), 0x2A, 0x00}; // wrong PEC
  transport.write(frame, 3);

  TEST_ASSERT_EQUAL_UINT8(kDefaultAddress, node.address()); // SET_ADDR NOT applied
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Error), static_cast<int>(node.status()));
}

// EP_SELECT paging: descriptor window reflects whichever endpoint is
// currently selected, and an out-of-range index is a No-Op + Error — spec §4.2.
void test_ep_select_paging_and_invalid_index() {
  OgrNode node(ClassId::Custom, false);
  int moisture = node.addEndpoint(TypeId::Moisture, Mode::Read, Format::Raw16, readMoisture);
  int relay = node.addEndpoint(TypeId::Relay, Mode::Write, Format::Binary, nullptr, writeRelay);
  TEST_ASSERT_EQUAL_INT(0, moisture);
  TEST_ASSERT_EQUAL_INT(1, relay);

  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();
  uint8_t addrData[] = {0x10};
  uint8_t addrFrame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), addrData, 1, addrFrame);
  transport.write(addrFrame, 3);

  // Select endpoint 1 (relay).
  uint8_t selData[] = {1};
  uint8_t selFrame[kMaxWriteFrame];
  buildWriteFrame(0x10, static_cast<uint8_t>(Reg::EpSelect), selData, 1, selFrame);
  transport.write(selFrame, 3);

  uint8_t outBuf[kMaxReadFrame];
  uint8_t n = transport.read(static_cast<uint8_t>(Reg::EpDescriptor), outBuf, sizeof(outBuf));
  TEST_ASSERT_EQUAL_UINT8(kEpDescriptorWidth + 1, n);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(TypeId::Relay), outBuf[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Mode::Write), outBuf[1]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Format::Binary), outBuf[2]);
  uint8_t expectedPec = expectedReadPec(0x10, static_cast<uint8_t>(Reg::EpDescriptor), outBuf, kEpDescriptorWidth);
  TEST_ASSERT_EQUAL_HEX8(expectedPec, outBuf[3]);

  // Invalid index: selection unchanged, STATUS flips to Error.
  uint8_t badSelData[] = {5};
  uint8_t badSelFrame[kMaxWriteFrame];
  buildWriteFrame(0x10, static_cast<uint8_t>(Reg::EpSelect), badSelData, 1, badSelFrame);
  transport.write(badSelFrame, 3);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Error), static_cast<int>(node.status()));

  n = transport.read(static_cast<uint8_t>(Reg::EpDescriptor), outBuf, sizeof(outBuf));
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(TypeId::Relay), outBuf[0]); // still endpoint 1
}

// Reading the data window calls the endpoint's onRead and packs the result
// little-endian into 4 bytes, per spec §5.1/§4.2.
void test_ep_data_read_packs_little_endian() {
  OgrNode node(ClassId::Custom, false);
  node.addEndpoint(TypeId::Moisture, Mode::Read, Format::Raw16, readMoisture);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();
  uint8_t addrData[] = {0x11};
  uint8_t addrFrame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), addrData, 1, addrFrame);
  transport.write(addrFrame, 3);

  fixedMoistureReading = 0x1234;
  uint8_t outBuf[kMaxReadFrame];
  uint8_t n = transport.read(static_cast<uint8_t>(Reg::EpData), outBuf, sizeof(outBuf));
  TEST_ASSERT_EQUAL_UINT8(kEpDataWidth + 1, n);
  TEST_ASSERT_EQUAL_HEX8(0x34, outBuf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, outBuf[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, outBuf[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, outBuf[3]);
}

// Writing the data window calls the endpoint's onWrite with the unpacked value.
void test_ep_data_write_invokes_callback() {
  OgrNode node(ClassId::Custom, false);
  node.addEndpoint(TypeId::Relay, Mode::Write, Format::Binary, nullptr, writeRelay);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();
  uint8_t addrData[] = {0x12};
  uint8_t addrFrame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), addrData, 1, addrFrame);
  transport.write(addrFrame, 3);

  uint8_t data[] = {0x01, 0x00, 0x00, 0x00};
  uint8_t frame[kMaxWriteFrame];
  buildWriteFrame(0x12, static_cast<uint8_t>(Reg::EpData), data, 4, frame);
  transport.write(frame, 6);

  TEST_ASSERT_EQUAL_INT(1, relayWriteCalls);
  TEST_ASSERT_EQUAL_UINT32(1, lastRelayValue);
}

// PLANT_UID is staged in RAM immediately (so reads never block on storage)
// and flushed from update() (so a slow EEPROM/flash write never happens on
// the transport's call path) — see OgrNode::update().
void test_plant_uid_persists_via_storage_and_defers_flush() {
  OgrNode node(ClassId::PlantContainer, true);
  node.onPlantUidChanged(onPlantUidChanged);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();
  uint8_t addrData[] = {0x13};
  uint8_t addrFrame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), addrData, 1, addrFrame);
  transport.write(addrFrame, 3);

  // PLANT_UID0..3 are four independently-written byte registers (spec §4.1)
  // — set all four so the assembled 32-bit UID is well-defined, rather than
  // leaving the upper bytes at the initial NO_UID sentinel's 0xFF.
  uint8_t b0[] = {0x78};
  uint8_t f0[kMaxWriteFrame];
  buildWriteFrame(0x13, static_cast<uint8_t>(Reg::PlantUid0), b0, 1, f0);
  transport.write(f0, 3);
  TEST_ASSERT_EQUAL_INT(0, storage.saveCount); // not flushed yet
  TEST_ASSERT_EQUAL_INT(1, plantUidCalls);

  for (uint8_t reg = static_cast<uint8_t>(Reg::PlantUid1); reg <= static_cast<uint8_t>(Reg::PlantUid3); ++reg) {
    uint8_t b[] = {0x00};
    uint8_t f[kMaxWriteFrame];
    buildWriteFrame(0x13, reg, b, 1, f);
    transport.write(f, 3);
  }

  node.update(); // flush happens here, off the transport call path
  TEST_ASSERT_EQUAL_INT(1, storage.saveCount);
  TEST_ASSERT_EQUAL_UINT32(0x78, storage.stored);

  uint8_t outBuf[kMaxReadFrame];
  transport.read(static_cast<uint8_t>(Reg::PlantUid0), outBuf, sizeof(outBuf));
  TEST_ASSERT_EQUAL_HEX8(0x78, outBuf[0]);
}

// Non-anchor modules opting out of UID storage (2-arg begin()) always report
// NO_UID and silently ignore writes — spec §4.1.
void test_no_storage_overload_ignores_plant_uid() {
  OgrNode node(ClassId::Light, false);
  node.begin(transport, gpio); // no storage
  gpio.enIn = true;
  node.update();
  uint8_t addrData[] = {0x14};
  uint8_t addrFrame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), addrData, 1, addrFrame);
  transport.write(addrFrame, 3);

  uint8_t b0[] = {0x78};
  uint8_t f0[kMaxWriteFrame];
  buildWriteFrame(0x14, static_cast<uint8_t>(Reg::PlantUid0), b0, 1, f0);
  transport.write(f0, 3);

  uint8_t outBuf[kMaxReadFrame];
  transport.read(static_cast<uint8_t>(Reg::PlantUid0), outBuf, sizeof(outBuf));
  TEST_ASSERT_EQUAL_HEX8(0xFF, outBuf[0]); // kNoPlantUid low byte, write was ignored
}

// RESTART drops the node back to UNASSIGNED and stops answering on the bus —
// spec §3.4. The master rediscovers it via the ordinary scan-for-new cycle.
void test_restart_returns_to_waiting_for_en_in() {
  OgrNode node(ClassId::Custom, false);
  node.onRestart(onRestart);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();
  uint8_t addrData[] = {0x15};
  uint8_t addrFrame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), addrData, 1, addrFrame);
  transport.write(addrFrame, 3);
  TEST_ASSERT_TRUE(gpio.enOut);

  uint8_t restartData[] = {static_cast<uint8_t>(RestartType::Soft)};
  uint8_t restartFrame[kMaxWriteFrame];
  buildWriteFrame(0x15, static_cast<uint8_t>(Reg::Restart), restartData, 1, restartFrame);
  transport.write(restartFrame, 3);

  TEST_ASSERT_EQUAL_INT(1, restartCalls);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RestartType::Soft), lastRestartType);
  TEST_ASSERT_FALSE(gpio.enOut);
  TEST_ASSERT_EQUAL_UINT8(0, node.address()); // updated immediately; transport_->end() itself is deferred

  // EN_IN is still high (this module's own restart doesn't affect its own
  // input), so the very next update() both flushes the deferred transport
  // end() *and* immediately re-enumerates at the default address again, in
  // that one call — matching how a module whose EN_IN never actually
  // dropped reappears on the bus right away (spec §3.4).
  node.update();
  TEST_ASSERT_TRUE(transport.listening);
  TEST_ASSERT_EQUAL_UINT8(kDefaultAddress, node.address());
}

// Regression test: SET_ADDR/RESTART must never touch the transport
// (setAddress()/end()) synchronously from onI2cWrite() — that's the ISR call
// path on real hardware, and a software-driven peripheral (e.g. AVR USI)
// reinitializing its own interrupt state machine while still inside the
// interrupt that triggered it has been observed to wedge it permanently.
// Both calls must land only from update() (plain loop() context). See the
// member comments in OgrNode.h.
void test_set_addr_and_restart_defer_transport_reconfig_to_update() {
  OgrNode node(ClassId::Custom, false);
  node.begin(transport, gpio, storage);
  gpio.enIn = true;
  node.update();

  uint8_t data[] = {0x2A};
  uint8_t frame[kMaxWriteFrame];
  buildWriteFrame(kDefaultAddress, static_cast<uint8_t>(Reg::SetAddr), data, 1, frame);
  transport.write(frame, 3); // stands in for the ISR-driven onI2cWrite call

  // address_ (pure in-memory state) updates immediately...
  TEST_ASSERT_EQUAL_UINT8(0x2A, node.address());
  // ...but the transport itself must not have been touched yet.
  TEST_ASSERT_EQUAL_UINT8(kDefaultAddress, transport.currentAddress);

  node.update(); // only now is it safe
  TEST_ASSERT_EQUAL_UINT8(0x2A, transport.currentAddress);

  uint8_t restartData[] = {static_cast<uint8_t>(RestartType::Soft)};
  uint8_t restartFrame[kMaxWriteFrame];
  buildWriteFrame(0x2A, static_cast<uint8_t>(Reg::Restart), restartData, 1, restartFrame);
  transport.write(restartFrame, 3);

  TEST_ASSERT_EQUAL_UINT8(0, node.address());
  TEST_ASSERT_TRUE(transport.listening); // transport_->end() not called yet

  gpio.enIn = false; // keep this update() call to just the deferred end()
  node.update();
  TEST_ASSERT_FALSE(transport.listening);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_waits_for_en_in_before_listening);
  RUN_TEST(test_set_addr_assigns_and_raises_en_out);
  RUN_TEST(test_bad_pec_sets_status_error_and_is_discarded);
  RUN_TEST(test_ep_select_paging_and_invalid_index);
  RUN_TEST(test_ep_data_read_packs_little_endian);
  RUN_TEST(test_ep_data_write_invokes_callback);
  RUN_TEST(test_plant_uid_persists_via_storage_and_defers_flush);
  RUN_TEST(test_no_storage_overload_ignores_plant_uid);
  RUN_TEST(test_restart_returns_to_waiting_for_en_in);
  RUN_TEST(test_set_addr_and_restart_defer_transport_reconfig_to_update);
  return UNITY_END();
}
