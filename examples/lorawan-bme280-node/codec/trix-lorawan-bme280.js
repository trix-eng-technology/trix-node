// Payload codec for lorawan-bme280-node, payload v1.
// Paste this file as-is into ChirpStack v4 (device profile > Codec >
// JavaScript functions) or TTN v3 (end device > Payload formatters > Custom
// Javascript). Plain ES5 so both runtimes accept it.
//
// Output keys follow the TRIX unit-suffix table (IoT-Standard-v1):
//   temperature_c humidity_pct pressure_hpa battery_v sensor_error
// A field the node marks "not available" is left out, not sent as 0.
// Layout: see lib/TrixLorawanPayload/src/trix_lorawan_payload.h

var FPORT_UPLINK = 1;
var FPORT_CONFIG = 10;
var INTERVAL_MIN = 60;
var INTERVAL_MAX = 3600;

function u16(b, i) {
  return (b[i] << 8) | b[i + 1];
}

function i16(b, i) {
  var v = u16(b, i);
  return v & 0x8000 ? v - 0x10000 : v;
}

function round(v, digits) {
  var f = Math.pow(10, digits);
  return Math.round(v * f) / f;
}

function decodeUplink(input) {
  var b = input.bytes;
  if (input.fPort !== FPORT_UPLINK) {
    return { errors: ["unexpected fPort " + input.fPort] };
  }
  if (!b || b.length < 10) {
    return { errors: ["payload too short: " + (b ? b.length : 0) + " bytes"] };
  }
  if (b[0] !== 1) {
    return { errors: ["unsupported payload version " + b[0]] };
  }
  var data = {};
  var warnings = [];
  var t = i16(b, 1);
  var h = u16(b, 3);
  var p = u16(b, 5);
  var mv = u16(b, 7);
  if (t !== -32768) data.temperature_c = round(t / 100, 2);
  if (h !== 0xffff) data.humidity_pct = round(h / 10, 1);
  if (p !== 0xffff) data.pressure_hpa = round(p / 10, 1);
  if (mv !== 0) data.battery_v = round(mv / 1000, 3);
  data.sensor_error = (b[9] & 0x01) === 0x01;
  if (b.length > 10) warnings.push("ignored " + (b.length - 10) + " trailing bytes");
  return warnings.length ? { data: data, warnings: warnings } : { data: data };
}

// Downlink: {"interval_s": 300} on fPort 10.
function encodeDownlink(input) {
  var s = input.data ? input.data.interval_s : undefined;
  if (typeof s !== "number" || s !== Math.floor(s) || s < INTERVAL_MIN || s > INTERVAL_MAX) {
    return { errors: ["interval_s must be an integer from " + INTERVAL_MIN + " to " + INTERVAL_MAX] };
  }
  return { fPort: FPORT_CONFIG, bytes: [(s >> 8) & 0xff, s & 0xff] };
}

function decodeDownlink(input) {
  if (input.fPort !== FPORT_CONFIG || !input.bytes || input.bytes.length !== 2) {
    return { errors: ["not a config downlink"] };
  }
  return { data: { interval_s: u16(input.bytes, 0) } };
}
