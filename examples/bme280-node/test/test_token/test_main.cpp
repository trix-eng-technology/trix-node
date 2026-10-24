// Host-side test: the firmware's token check must accept exactly what the
// TRIX server signs (lib/ota-sign.ts) and nothing else.
//   pio test -e native
#include <string.h>
#include <unity.h>

#include "trix_token.h"

// RFC 8032 test vector 1 public key. The matching seed is public, so this
// key must never be used in a real build.
static const uint8_t PUB[32] = {
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};

// Produced by TRIX signManifest() with that seed for
// {"type":"bme280-node","version":"1.3.0","size":912384,"sha256":"abab..ab","exp":4102444800}
static const char* TOKEN =
    "eyJ0eXBlIjoiYm1lMjgwLW5vZGUiLCJ2ZXJzaW9uIjoiMS4zLjAiLCJzaXplIjo5MTIzODQsInNoYTI1NiI6ImFiYWJhYmFi"
    "YWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWIiLCJleHAiOjQxMDI0NDQ4"
    "MDB9.QClDKranq0c0w4NnOZDWIG4ZGZUVlrgF6kAD5utf3CNTFDbLeucFUAdvnsEBrxniU9QXahFUbPujwFiczmsVDg";

// Same payload but exp = 1700000000 (2023-11-14): signature valid, already expired.
static const char* EXPIRED =
    "eyJ0eXBlIjoiYm1lMjgwLW5vZGUiLCJ2ZXJzaW9uIjoiMS4zLjAiLCJzaXplIjo5MTIzODQsInNoYTI1NiI6ImFiYWJhYmFi"
    "YWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWIiLCJleHAiOjE3MDAwMDAw"
    "MDB9.QHZj3aiHbIDZoxTaN_VLjzoS8VM1clE02S9L7kmOTMMpuOPYw5igzLu9bk8SP25u-mHjEViCZUWPdbPFiocGCg";

// Validly signed but without exp, as a pre-exp server would issue: refused.
static const char* NO_EXP =
    "eyJ0eXBlIjoiYm1lMjgwLW5vZGUiLCJ2ZXJzaW9uIjoiMS4zLjAiLCJzaXplIjo5MTIzODQsInNoYTI1NiI6ImFiYWJhYmFi"
    "YWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWJhYmFiYWIifQ.wMitY6CDvBxh3T971"
    "lI2UgRTm8eQmhy174mrLFy8MVrpAT1WZminlDs_2YvLWSQ3xjxUQLDCXjYktI34XLCPDQ";

void setUp() {}
void tearDown() {}

#include <Ed25519.h>

// RFC 8032 section 7.1 test 1: empty message. Proves the Ed25519 build itself.
static void test_rfc8032_vector() {
  static const uint8_t sig[64] = {
      0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72, 0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
      0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74, 0xd8, 0x73, 0xe0, 0x65, 0x22, 0x49, 0x01, 0x55,
      0x5f, 0xb8, 0x82, 0x15, 0x90, 0xa3, 0x3b, 0xac, 0xc6, 0x1e, 0x39, 0x70, 0x1c, 0xf9, 0xb4, 0x6b,
      0xd2, 0x5b, 0xf5, 0xf0, 0x59, 0x5b, 0xbe, 0x24, 0x65, 0x51, 0x41, 0x43, 0x8e, 0x7a, 0x10, 0x0b};
  TEST_ASSERT_TRUE(Ed25519::verify(sig, PUB, "", 0));
}

// Same check as verifyToken, step by step, so a failure says which step.
static void test_token_steps() {
  const char* dot = strchr(TOKEN, '.');
  TEST_ASSERT_NOT_NULL(dot);
  uint8_t payload[320], sig[66];
  int pLen = trix::b64decode(TOKEN, dot - TOKEN, payload, sizeof(payload) - 1);
  int sLen = trix::b64decode(dot + 1, strlen(dot + 1), sig, sizeof(sig));
  TEST_ASSERT_GREATER_THAN(100, pLen);
  TEST_ASSERT_EQUAL_INT(64, sLen);
  payload[pLen] = 0;
  TEST_ASSERT_EQUAL_STRING_LEN("{\"type\":\"bme280-node\"", (const char*)payload, 21);
  TEST_ASSERT_TRUE(Ed25519::verify(sig, PUB, payload, pLen));
}

static void test_accepts_server_token() {
  trix::SignedManifest m;
  TEST_ASSERT_TRUE(trix::verifyToken(TOKEN, PUB, m));
  TEST_ASSERT_EQUAL_STRING("bme280-node", m.type);
  TEST_ASSERT_EQUAL_STRING("1.3.0", m.version);
  TEST_ASSERT_EQUAL_UINT32(912384, m.size);
  TEST_ASSERT_EQUAL_UINT(64, strlen(m.sha256));
  TEST_ASSERT_EQUAL_STRING_LEN("abab", m.sha256, 4);
  TEST_ASSERT_EQUAL_UINT32(4102444800UL, m.exp);
}

// The token check exposes exp; TrixNode refuses when now > exp.
static void test_expired_token_exposes_exp() {
  trix::SignedManifest m;
  TEST_ASSERT_TRUE(trix::verifyToken(EXPIRED, PUB, m));
  TEST_ASSERT_EQUAL_UINT32(1700000000UL, m.exp);
}

static void test_rejects_tampered_signature() {
  char t[400];
  strcpy(t, TOKEN);
  size_t n = strlen(t);
  t[n - 3] = (t[n - 3] == 'A') ? 'B' : 'A';
  trix::SignedManifest m;
  TEST_ASSERT_FALSE(trix::verifyToken(t, PUB, m));
}

static void test_rejects_tampered_payload() {
  char t[400];
  strcpy(t, TOKEN);
  t[10] = (t[10] == 'A') ? 'B' : 'A';
  trix::SignedManifest m;
  TEST_ASSERT_FALSE(trix::verifyToken(t, PUB, m));
}

static void test_rejects_token_without_exp() {
  trix::SignedManifest m;
  TEST_ASSERT_FALSE(trix::verifyToken(NO_EXP, PUB, m));
}

static void test_rejects_wrong_key() {
  uint8_t other[32];
  memcpy(other, PUB, 32);
  other[0] ^= 0x01;
  trix::SignedManifest m;
  TEST_ASSERT_FALSE(trix::verifyToken(TOKEN, other, m));
}

static void test_rejects_garbage() {
  trix::SignedManifest m;
  TEST_ASSERT_FALSE(trix::verifyToken("", PUB, m));
  TEST_ASSERT_FALSE(trix::verifyToken("abc", PUB, m));
  TEST_ASSERT_FALSE(trix::verifyToken("abc.def", PUB, m));
  TEST_ASSERT_FALSE(trix::verifyToken(nullptr, PUB, m));
}

static void test_semver() {
  TEST_ASSERT_TRUE(trix::semverGreater("1.3.0", "1.2.9"));
  TEST_ASSERT_TRUE(trix::semverGreater("2.0.0", "1.99.99"));
  TEST_ASSERT_TRUE(trix::semverGreater("1.10.0", "1.9.0"));
  TEST_ASSERT_FALSE(trix::semverGreater("1.3.0", "1.3.0"));
  TEST_ASSERT_FALSE(trix::semverGreater("1.2.0", "1.3.0"));
  // pre-release ordering, same as the server (npm semver)
  TEST_ASSERT_TRUE(trix::semverGreater("1.3.1-rc1", "1.3.0"));
  TEST_ASSERT_TRUE(trix::semverGreater("1.3.0", "1.3.0-rc1"));
  TEST_ASSERT_FALSE(trix::semverGreater("1.3.0-rc1", "1.3.0"));
  TEST_ASSERT_TRUE(trix::semverGreater("1.3.0-rc2", "1.3.0-rc1"));
  TEST_ASSERT_FALSE(trix::semverGreater("1.3.0+build7", "1.3.0"));
  // candidate must be valid; an unparsable current version can recover
  TEST_ASSERT_FALSE(trix::semverGreater("m2-1.7.6", "1.0.0"));
  TEST_ASSERT_FALSE(trix::semverGreater("1.3", "1.2.0"));
  TEST_ASSERT_TRUE(trix::semverGreater("1.3.0", "m2-1.7.6"));
  TEST_ASSERT_TRUE(trix::semverGreater("1.3.0", ""));
}

static void test_b64url() {
  uint8_t out[8];
  TEST_ASSERT_EQUAL_INT(3, trix::b64decode("-_-_", 4, out, sizeof(out)));
  TEST_ASSERT_EQUAL_HEX8(0xfb, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xff, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0xbf, out[2]);
  TEST_ASSERT_EQUAL_INT(-1, trix::b64decode("a*b", 3, out, sizeof(out)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rfc8032_vector);
  RUN_TEST(test_token_steps);
  RUN_TEST(test_accepts_server_token);
  RUN_TEST(test_expired_token_exposes_exp);
  RUN_TEST(test_rejects_tampered_signature);
  RUN_TEST(test_rejects_tampered_payload);
  RUN_TEST(test_rejects_token_without_exp);
  RUN_TEST(test_rejects_wrong_key);
  RUN_TEST(test_rejects_garbage);
  RUN_TEST(test_semver);
  RUN_TEST(test_b64url);
  return UNITY_END();
}
