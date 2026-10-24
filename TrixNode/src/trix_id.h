// Name rules shared by TrixNode and host tests, protocol trix-node/1.
// Portable (no Arduino headers) so it can be unit-tested on the host.
//
// Same rule as TRIX ID_RE (lib/monitor/protocol.ts): ^[a-z0-9][a-z0-9-]{0,47}$
// The server accepts looser topic ids, but devices must only ever create
// names that the admin pages can also create.
#pragma once
#include <stddef.h>

namespace trix {

static const size_t ID_MAX_LEN = 48;

inline bool validId(const char* s) {
  if (!s || !*s) return false;
  size_t n = 0;
  for (const char* p = s; *p; p++, n++) {
    const char c = *p;
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!alnum && !(c == '-' && n > 0)) return false;
    if (n + 1 > ID_MAX_LEN) return false;
  }
  return true;
}

// Channel names stay one topic level: no '/', no wildcards.
inline bool validChannel(const char* s) {
  if (!s || !*s) return false;
  size_t n = 0;
  for (const char* p = s; *p; p++, n++) {
    const char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    if (n + 1 > 32) return false;
  }
  return true;
}

}  // namespace trix
