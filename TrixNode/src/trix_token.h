// OTA manifest token check, protocol trix-node/1.
// Portable (no Arduino headers) so it can be unit-tested on the host.
//
//   token = base64url(payload) "." base64url(ed25519 signature over payload)
//   payload = {"type":..,"version":..,"size":..,"sha256":..,"exp":..}
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace trix {

struct SignedManifest {
  char type[49];
  char version[33];
  uint32_t size;
  char sha256[65];
  uint32_t exp;  // unix seconds; caller must refuse when now > exp
};

// Decodes base64 or base64url (padding optional). Returns bytes written or -1.
int b64decode(const char* in, size_t len, uint8_t* out, size_t outMax);

// Verifies the token signature with the 32-byte public key and copies the
// signed fields into `out`. Returns false on any malformed or forged token.
bool verifyToken(const char* token, const uint8_t pubkey[32], SignedManifest& out);

// True when `candidate` should replace `current`. `candidate` must be valid
// semver; an unparsable `current` (blank NVS, pre-standard "m2-1.7.6") is
// treated as older so the device can recover through OTA. Pre-release
// ordering follows semver: 1.3.0-rc1 < 1.3.0, rc2 > rc1. Build metadata
// (+...) is ignored. Matches shouldOfferUpdate() on the server.
bool semverGreater(const char* candidate, const char* current);

}  // namespace trix
