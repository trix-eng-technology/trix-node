// Mirror of test/vectors.json (by hand). codec/test.mjs fails if the bytes
// here and in the JSON drift apart, so the C encoder and the JS decoder are
// always tested against the same frames.
#pragma once
#include <stdint.h>

#include "trix_lorawan_payload.h"

struct UplinkVector {
  const char* name;
  TrixLwReading reading;
  uint8_t bytes[TRIX_LW_PAYLOAD_LEN];
};

static const UplinkVector UPLINK_VECTORS[] = {
    // VECTOR normal
    {"normal", {true, 25.31f, 61.2f, 1013.2f, 3987}, {1, 9, 227, 2, 100, 39, 148, 15, 147, 0}},
    // VECTOR negative_no_battery
    {"negative_no_battery", {true, -5.5f, 100.0f, 950.0f, 0}, {1, 253, 218, 3, 232, 37, 28, 0, 0, 0}},
    // VECTOR sensor_error
    {"sensor_error", {false, 0.0f, 0.0f, 0.0f, 3300}, {1, 128, 0, 255, 255, 255, 255, 12, 228, 1}},
    // VECTOR rounding
    {"rounding", {true, 0.004f, 45.06f, 1013.25f, 1}, {1, 0, 0, 1, 195, 39, 149, 0, 1, 0}},
};

struct DownlinkVector {
  uint16_t intervalS;
  uint8_t bytes[2];
};

static const DownlinkVector DOWNLINK_VECTORS[] = {
    {300, {1, 44}},
    {3600, {14, 16}},
    {60, {0, 60}},
};
