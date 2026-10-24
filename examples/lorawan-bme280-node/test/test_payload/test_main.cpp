// Host-side test: the node's encoder must produce exactly the frames the
// ChirpStack/TTN codec is tested against (test/vectors.json).
//   pio test -e native
#include <string.h>
#include <unity.h>

#include "trix_lorawan_payload.h"
#include "vectors.h"

void setUp() {}
void tearDown() {}

static void test_uplink_vectors() {
  for (const UplinkVector& v : UPLINK_VECTORS) {
    uint8_t out[TRIX_LW_PAYLOAD_LEN] = {0};
    TEST_ASSERT_EQUAL_MESSAGE(TRIX_LW_PAYLOAD_LEN, trixLwEncode(v.reading, out, sizeof(out)), v.name);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.bytes, out, TRIX_LW_PAYLOAD_LEN, v.name);
  }
}

static void test_encode_rejects_short_buffer() {
  uint8_t out[TRIX_LW_PAYLOAD_LEN - 1];
  TEST_ASSERT_EQUAL(0, trixLwEncode(UPLINK_VECTORS[0].reading, out, sizeof(out)));
}

static void test_saturates_instead_of_wrapping() {
  TrixLwReading r = {true, 400.0f, 120.0f, -3.0f, 4200};
  uint8_t out[TRIX_LW_PAYLOAD_LEN];
  trixLwEncode(r, out, sizeof(out));
  const uint8_t expected[TRIX_LW_PAYLOAD_LEN] = {1, 0x7F, 0xFF, 0x03, 0xE8, 0, 0, 0x10, 0x68, 0};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TRIX_LW_PAYLOAD_LEN);
}

static void test_nan_field_is_not_available() {
  TrixLwReading r = {true, NAN, 50.0f, 1000.0f, 0};
  uint8_t out[TRIX_LW_PAYLOAD_LEN];
  trixLwEncode(r, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0x80, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
}

static void test_downlink_vectors() {
  for (const DownlinkVector& v : DOWNLINK_VECTORS) {
    uint16_t got = 0;
    TEST_ASSERT_TRUE(trixLwParseConfig(TRIX_LW_FPORT_CONFIG, v.bytes, 2, &got));
    TEST_ASSERT_EQUAL_UINT16(v.intervalS, got);
  }
}

static void test_downlink_clamps_and_rejects() {
  uint16_t got = 0;
  const uint8_t tooShort[2] = {0, 30};
  TEST_ASSERT_TRUE(trixLwParseConfig(TRIX_LW_FPORT_CONFIG, tooShort, 2, &got));
  TEST_ASSERT_EQUAL_UINT16(TRIX_LW_INTERVAL_MIN, got);
  const uint8_t tooLong[2] = {0xFF, 0xFF};
  TEST_ASSERT_TRUE(trixLwParseConfig(TRIX_LW_FPORT_CONFIG, tooLong, 2, &got));
  TEST_ASSERT_EQUAL_UINT16(TRIX_LW_INTERVAL_MAX, got);
  got = 123;
  TEST_ASSERT_FALSE(trixLwParseConfig(TRIX_LW_FPORT_UPLINK, tooLong, 2, &got));
  TEST_ASSERT_FALSE(trixLwParseConfig(TRIX_LW_FPORT_CONFIG, tooLong, 1, &got));
  TEST_ASSERT_FALSE(trixLwParseConfig(TRIX_LW_FPORT_CONFIG, tooLong, 3, &got));
  TEST_ASSERT_EQUAL_UINT16(123, got);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_uplink_vectors);
  RUN_TEST(test_encode_rejects_short_buffer);
  RUN_TEST(test_saturates_instead_of_wrapping);
  RUN_TEST(test_nan_field_is_not_available);
  RUN_TEST(test_downlink_vectors);
  RUN_TEST(test_downlink_clamps_and_rejects);
  return UNITY_END();
}
