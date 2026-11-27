#include "rekit/sign.h"

#include <cctype>
#include <cstdint>

extern "C" {
#include "monocypher/monocypher-ed25519.h"
}

namespace rekit {
namespace {

bool from_hex(const std::string& hex, uint8_t* out, size_t n) {
  if (hex.size() != n * 2) return false;
  const auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < n; ++i) {
    const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  return true;
}

std::string trimmed(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

}  // namespace

bool is_public_key(const std::string& hex) {
  uint8_t k[32];
  return from_hex(hex, k, 32);
}

std::string signed_by(const std::string& message, const std::string& sig_text, const std::vector<std::string>& keys) {
  if (sig_text.size() > 4096) return {};  // a few lines at most
  for (size_t at = 0; at <= sig_text.size();) {
    const size_t nl = std::min(sig_text.find('\n', at), sig_text.size());
    const std::string line = trimmed(sig_text.substr(at, nl - at));
    at = nl + 1;
    uint8_t sig[64];
    if (!from_hex(line, sig, 64)) continue;
    for (const std::string& k : keys) {
      uint8_t pk[32];
      if (from_hex(k, pk, 32) &&
          crypto_ed25519_check(sig, pk, reinterpret_cast<const uint8_t*>(message.data()), message.size()) == 0)
        return k;
    }
  }
  return {};
}

}  // namespace rekit
