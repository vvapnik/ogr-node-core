#include <unity.h>
#include "OgrCrc8.h"

using ogr::Crc8;
using ogr::Crc8Builder;

void setUp() {}
void tearDown() {}

// Standard CRC-8/SMBUS check value for ASCII "123456789" (poly 0x07, init
// 0x00, no reflection, no final XOR) — the canonical vector for this variant.
void test_check_vector() {
  const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX8(0xF4, Crc8::compute(input, sizeof(input)));
}

void test_empty_input_is_seed() {
  TEST_ASSERT_EQUAL_HEX8(0x00, Crc8::compute(nullptr, 0));
}

void test_builder_matches_one_shot_compute() {
  const uint8_t input[] = {0x54, 0x06, 0x01};
  uint8_t expected = Crc8::compute(input, sizeof(input));

  Crc8Builder b;
  b.add(0x54);
  b.add(0x06);
  b.add(0x01);
  TEST_ASSERT_EQUAL_HEX8(expected, b.value());
}

void test_builder_add_buffer_matches_add_byte() {
  const uint8_t input[] = {0x10, 0x20, 0x30, 0x40};

  Crc8Builder byBuffer;
  byBuffer.add(input, sizeof(input));

  Crc8Builder byByte;
  for (uint8_t b : input) byByte.add(b);

  TEST_ASSERT_EQUAL_HEX8(byByte.value(), byBuffer.value());
}

void test_single_bit_change_flips_crc() {
  const uint8_t a[] = {0x54, 0x06, 0x01};
  const uint8_t b[] = {0x54, 0x06, 0x03}; // one bit different in last byte
  TEST_ASSERT_NOT_EQUAL(Crc8::compute(a, sizeof(a)), Crc8::compute(b, sizeof(b)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_check_vector);
  RUN_TEST(test_empty_input_is_seed);
  RUN_TEST(test_builder_matches_one_shot_compute);
  RUN_TEST(test_builder_add_buffer_matches_add_byte);
  RUN_TEST(test_single_bit_change_flips_crc);
  return UNITY_END();
}
