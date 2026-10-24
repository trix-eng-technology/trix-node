// LoRaWAN OTAA credentials, one set per device.
//
//   cp include/lorawan_keys.example.h include/lorawan_keys.h
//
// then copy DevEUI, JoinEUI and AppKey from ChirpStack (device > OTA keys,
// LoRaWAN 1.0.x: only the Application key is used) and delete the
// LORAWAN_KEYS_PLACEHOLDER line. lorawan_keys.h is in .gitignore; keep it
// out of git and out of shared folders.
//
// With the placeholder line still present the firmware builds and runs but
// refuses to join, so a board flashed by mistake never sends a join request
// with all-zero keys.
#pragma once
#include <stdint.h>

#define LORAWAN_KEYS_PLACEHOLDER 1

// MSB first, as ChirpStack shows them.
static const uint64_t LORAWAN_JOIN_EUI = 0x0000000000000000ULL;
static const uint64_t LORAWAN_DEV_EUI = 0x0000000000000000ULL;
static const uint8_t LORAWAN_APP_KEY[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
