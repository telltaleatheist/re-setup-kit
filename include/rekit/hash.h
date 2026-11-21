#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// SHA-256 and MD5 (from mw3-re's patch_import): files checked against known sums.
namespace rekit {
std::string sha256_hex(const uint8_t* data, size_t n);
// A file's SHA-256 as hex, read a megabyte at a time; empty when it cannot be read.
std::string sha256_file_hex(const std::string& path);
// The same over data given in pieces (a mod's files, EXPORT and IMPORT);
// hex() once, at the end.
struct Sha256 {
  void add(const uint8_t* data, size_t n);
  std::string hex();

 private:
  void block(const uint8_t* p);
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t buf[64] = {};
  size_t used = 0;
  uint64_t total = 0;
};
void md5(const uint8_t* data, size_t n, uint8_t out[16]);
}  // namespace rekit
