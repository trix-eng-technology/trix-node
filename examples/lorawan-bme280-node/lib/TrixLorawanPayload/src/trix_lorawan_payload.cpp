#include "trix_lorawan_payload.h"

#include <math.h>

static void putU16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFF);
}

// Rounds and saturates so a wild sensor reading never wraps into a sentinel.
static int32_t scaled(float v, float scale) { return (int32_t)lroundf(v * scale); }

static int16_t encodeTemp(float c) {
  if (isnan(c)) return TRIX_LW_TEMP_NA;
  int32_t t = scaled(c, 100.0f);
  if (t < -32767) t = -32767;
  if (t > 32767) t = 32767;
  return (int16_t)t;
}

static uint16_t encodeU16(float v, float scale, int32_t maxValid) {
  if (isnan(v)) return TRIX_LW_U16_NA;
  int32_t x = scaled(v, scale);
  if (x < 0) x = 0;
  if (x > maxValid) x = maxValid;
  return (uint16_t)x;
}

size_t trixLwEncode(const TrixLwReading& r, uint8_t* out, size_t outSize) {
  if (outSize < TRIX_LW_PAYLOAD_LEN) return 0;
  int16_t t = TRIX_LW_TEMP_NA;
  uint16_t h = TRIX_LW_U16_NA, p = TRIX_LW_U16_NA;
  if (r.sensorOk) {
    t = encodeTemp(r.temperatureC);
    h = encodeU16(r.humidityPct, 10.0f, 1000);
    p = encodeU16(r.pressureHpa, 10.0f, 0xFFFE);
  }
  out[0] = TRIX_LW_PAYLOAD_VERSION;
  putU16(out + 1, (uint16_t)t);
  putU16(out + 3, h);
  putU16(out + 5, p);
  putU16(out + 7, r.batteryMv);
  out[9] = r.sensorOk ? 0 : TRIX_LW_FLAG_SENSOR_ERROR;
  return TRIX_LW_PAYLOAD_LEN;
}

bool trixLwParseConfig(uint8_t fPort, const uint8_t* data, size_t len, uint16_t* intervalS) {
  if (fPort != TRIX_LW_FPORT_CONFIG || len != 2 || !data || !intervalS) return false;
  uint16_t v = (uint16_t)((data[0] << 8) | data[1]);
  if (v < TRIX_LW_INTERVAL_MIN) v = TRIX_LW_INTERVAL_MIN;
  if (v > TRIX_LW_INTERVAL_MAX) v = TRIX_LW_INTERVAL_MAX;
  *intervalS = v;
  return true;
}
