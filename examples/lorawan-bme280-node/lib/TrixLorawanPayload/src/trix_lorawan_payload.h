// Payload v1 of lorawan-bme280-node. Portable C++ (no Arduino) so the
// encoder is tested on the host against the same vectors as the JS codec.
//
// Uplink, fPort 1, 10 bytes, big-endian:
//   0     uint8   version = 1
//   1..2  int16   temperature, 0.01 C      0x8000 = not available
//   3..4  uint16  humidity, 0.1 %          0xFFFF = not available
//   5..6  uint16  pressure, 0.1 hPa        0xFFFF = not available
//   7..8  uint16  battery, mV              0      = not measured
//   9     uint8   flags: bit0 sensor_error
//
// Downlink, fPort 10, 2 bytes: uint16 interval_s, big-endian.
// The node clamps it to [TRIX_LW_INTERVAL_MIN, TRIX_LW_INTERVAL_MAX].
#pragma once
#include <stddef.h>
#include <stdint.h>

#define TRIX_LW_PAYLOAD_VERSION 1
#define TRIX_LW_PAYLOAD_LEN 10
#define TRIX_LW_FPORT_UPLINK 1
#define TRIX_LW_FPORT_CONFIG 10
#define TRIX_LW_INTERVAL_MIN 60
#define TRIX_LW_INTERVAL_MAX 3600

#define TRIX_LW_TEMP_NA ((int16_t)0x8000)
#define TRIX_LW_U16_NA ((uint16_t)0xFFFF)
#define TRIX_LW_FLAG_SENSOR_ERROR 0x01

struct TrixLwReading {
  bool sensorOk;         // false: temperature/humidity/pressure are sent as "not available"
  float temperatureC;
  float humidityPct;
  float pressureHpa;
  uint16_t batteryMv;    // 0 = not measured
};

// Writes TRIX_LW_PAYLOAD_LEN bytes; returns the length, or 0 if out is too small.
size_t trixLwEncode(const TrixLwReading& r, uint8_t* out, size_t outSize);

// Parses a config downlink. Returns true and the clamped interval when the
// port and length match; false otherwise (the caller keeps its interval).
bool trixLwParseConfig(uint8_t fPort, const uint8_t* data, size_t len, uint16_t* intervalS);
