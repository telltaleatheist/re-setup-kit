#pragma once
// Test keys only (seeded from one byte): a key pair and a signature, through Monocypher's own signer, for the
// checks of signed data. Never a real key.
#include <cstdint>
#include <cstdio>
#include <string>

extern "C" {
#include "monocypher/monocypher-ed25519.h"
}

namespace test_keys {

inline std::string hex(const uint8_t* b, size_t n) {
  std::string s;
  char x[3];
  for (size_t i = 0; i < n; ++i) std::snprintf(x, sizeof x, "%02x", b[i]), s += x;
  return s;
}
inline void pair(uint8_t seed_byte, uint8_t secret[64], uint8_t pub[32]) {
  uint8_t seed[32];
  for (uint8_t& b : seed) b = seed_byte;
  crypto_ed25519_key_pair(secret, pub, seed);
}
inline std::string public_key(uint8_t seed_byte) {
  uint8_t s[64], p[32];
  pair(seed_byte, s, p);
  return hex(p, 32);
}
// One line of a .sig file: `message` signed by the test key `seed_byte`.
inline std::string sign(const std::string& message, uint8_t seed_byte) {
  uint8_t s[64], p[32], sig[64];
  pair(seed_byte, s, p);
  crypto_ed25519_sign(sig, s, reinterpret_cast<const uint8_t*>(message.data()), message.size());
  return hex(sig, 64) + "\n";
}

}  // namespace test_keys
