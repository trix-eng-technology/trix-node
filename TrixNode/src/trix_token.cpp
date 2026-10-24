#include "trix_token.h"

#include <ArduinoJson.h>
#include <Ed25519.h>
#include <stdlib.h>
#include <string.h>

namespace trix {

static int b64val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}

int b64decode(const char* in, size_t len, uint8_t* out, size_t outMax) {
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  for (size_t i = 0; i < len; i++) {
    if (in[i] == '=') break;
    int v = b64val(in[i]);
    if (v < 0) return -1;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= outMax) return -1;
      out[n++] = (uint8_t)(acc >> bits);
    }
  }
  return (int)n;
}

static bool copyField(JsonDocument& doc, const char* key, char* dst, size_t dstSize) {
  const char* v = doc[key] | (const char*)nullptr;
  if (!v || strlen(v) >= dstSize) return false;
  strcpy(dst, v);
  return true;
}

bool verifyToken(const char* token, const uint8_t pubkey[32], SignedManifest& out) {
  if (!token) return false;
  const char* dot = strchr(token, '.');
  if (!dot) return false;

  uint8_t payload[320];
  uint8_t sig[66];
  int pLen = b64decode(token, (size_t)(dot - token), payload, sizeof(payload) - 1);
  int sLen = b64decode(dot + 1, strlen(dot + 1), sig, sizeof(sig));
  if (pLen <= 0 || sLen != 64) return false;
  if (!Ed25519::verify(sig, pubkey, payload, (size_t)pLen)) return false;

  payload[pLen] = 0;
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)payload)) return false;
  if (!copyField(doc, "type", out.type, sizeof(out.type))) return false;
  if (!copyField(doc, "version", out.version, sizeof(out.version))) return false;
  if (!copyField(doc, "sha256", out.sha256, sizeof(out.sha256))) return false;
  if (strlen(out.sha256) != 64) return false;
  long size = doc["size"] | -1L;
  if (size <= 0) return false;
  out.size = (uint32_t)size;
  long long exp = doc["exp"] | 0LL;
  if (exp <= 0 || exp > 0xFFFFFFFFLL) return false;  // unsigned manifests without expiry are refused
  out.exp = (uint32_t)exp;
  return true;
}

// Parses MAJOR.MINOR.PATCH[-pre][+build]; `pre` points at the pre-release
// text (empty when none), ending before any '+'.
static bool parseSemver(const char* s, long v[3], const char** pre, size_t* preLen) {
  if (!s) return false;
  for (int i = 0; i < 3; i++) {
    if (*s < '0' || *s > '9') return false;
    char* end = nullptr;
    v[i] = strtol(s, &end, 10);
    s = end;
    if (i < 2) {
      if (*s != '.') return false;
      s++;
    }
  }
  *pre = "";
  *preLen = 0;
  if (*s == '-') {
    s++;
    const char* plus = strchr(s, '+');
    *pre = s;
    *preLen = plus ? (size_t)(plus - s) : strlen(s);
    if (*preLen == 0) return false;
    return true;
  }
  return *s == 0 || *s == '+';
}

bool semverGreater(const char* candidate, const char* current) {
  long a[3], b[3];
  const char *pa, *pb;
  size_t la, lb;
  if (!parseSemver(candidate, a, &pa, &la)) return false;
  if (!parseSemver(current, b, &pb, &lb)) return true;
  for (int i = 0; i < 3; i++) {
    if (a[i] != b[i]) return a[i] > b[i];
  }
  if (la == 0 || lb == 0) return la == 0 && lb > 0;  // release beats its pre-release
  int c = strncmp(pa, pb, la < lb ? la : lb);
  return c > 0 || (c == 0 && la > lb);
}

}  // namespace trix
