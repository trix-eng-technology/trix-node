// node --test codec/test.mjs   (Node 22 does not expand a bare directory)
// Runs the codec the way ChirpStack/TTN do (a plain script, functions in the
// global scope) against test/vectors.json, and checks that the C mirror
// test/test_payload/vectors.h carries the same bytes.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");
const vectors = JSON.parse(readFileSync(join(root, "test/vectors.json"), "utf8"));
const codec = vm.createContext({});
vm.runInContext(readFileSync(join(here, "trix-lorawan-bme280.js"), "utf8"), codec);

// The vm context has its own Object prototype; round-trip through JSON so
// deepStrictEqual compares plain values only.
const call = (fn, input) => JSON.parse(JSON.stringify(codec[fn](input)));

test("decodeUplink matches every vector", () => {
  for (const v of vectors.uplink) {
    assert.deepEqual(call("decodeUplink", { fPort: 1, bytes: v.bytes }), { data: v.decoded }, v.name);
  }
});

test("decodeUplink rejects wrong port, short frames and unknown versions", () => {
  const ok = vectors.uplink[0].bytes;
  assert.ok(call("decodeUplink", { fPort: 2, bytes: ok }).errors);
  assert.ok(call("decodeUplink", { fPort: 1, bytes: ok.slice(0, 9) }).errors);
  assert.ok(call("decodeUplink", { fPort: 1, bytes: [2, ...ok.slice(1)] }).errors);
});

test("decodeUplink keeps data and warns on trailing bytes", () => {
  const r = call("decodeUplink", { fPort: 1, bytes: [...vectors.uplink[0].bytes, 0xaa] });
  assert.deepEqual(r.data, vectors.uplink[0].decoded);
  assert.equal(r.warnings.length, 1);
});

test("encodeDownlink and decodeDownlink match every vector", () => {
  for (const v of vectors.downlink) {
    assert.deepEqual(call("encodeDownlink", { data: { interval_s: v.interval_s } }), { fPort: 10, bytes: v.bytes });
    assert.deepEqual(call("decodeDownlink", { fPort: 10, bytes: v.bytes }), { data: { interval_s: v.interval_s } });
  }
});

test("encodeDownlink rejects out-of-range or non-integer intervals", () => {
  for (const s of [59, 3601, 300.5, "300", undefined]) {
    assert.ok(call("encodeDownlink", { data: { interval_s: s } }).errors, String(s));
  }
});

test("C mirror vectors.h carries the same bytes as vectors.json", () => {
  const h = readFileSync(join(root, "test/test_payload/vectors.h"), "utf8");
  const arrays = (re) => [...h.matchAll(re)].map((m) => m[1].split(",").map((x) => Number(x.trim())));
  const up = arrays(/\{"[a-z_]+", \{[^}]*\}, \{([0-9, ]+)\}\}/g);
  assert.deepEqual(up, vectors.uplink.map((v) => v.bytes));
  const names = [...h.matchAll(/\{"([a-z_]+)", \{/g)].map((m) => m[1]);
  assert.deepEqual(names, vectors.uplink.map((v) => v.name));
  const down = [...h.matchAll(/\{(\d+), \{(\d+), (\d+)\}\}/g)].map((m) => ({
    interval_s: Number(m[1]),
    bytes: [Number(m[2]), Number(m[3])],
  }));
  assert.deepEqual(down, vectors.downlink);
});

test("C mirror readings match vectors.json readings", () => {
  const h = readFileSync(join(root, "test/test_payload/vectors.h"), "utf8");
  const rows = [...h.matchAll(/\{"[a-z_]+", \{(true|false), ([-0-9.]+)f, ([-0-9.]+)f, ([-0-9.]+)f, (\d+)\}/g)];
  assert.deepEqual(
    rows.map((m) => ({
      sensorOk: m[1] === "true",
      temperatureC: Number(m[2]),
      humidityPct: Number(m[3]),
      pressureHpa: Number(m[4]),
      batteryMv: Number(m[5]),
    })),
    vectors.uplink.map((v) => v.reading),
  );
});
