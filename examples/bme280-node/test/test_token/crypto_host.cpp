// Host build of the rweather/Crypto pieces Ed25519 verification needs.
// The full library pulls in Arduino.h through RNG.cpp, so the native test
// env ignores it and compiles only these units. Verification never draws
// random numbers; the RNG stub exists only to satisfy the linker.
#include "BigNumberUtil.cpp"
#include "Crypto.cpp"
#include "Curve25519.cpp"
#include "Ed25519.cpp"
#include "Hash.cpp"
#include "SHA512.cpp"
#include "RNG.h"

RNGClass RNG;
RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}
void RNGClass::rand(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) data[i] = 0;
}
