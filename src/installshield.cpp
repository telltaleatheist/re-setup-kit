#include "rekit/installshield.h"

#include <algorithm>
#include <cstring>

#include "rekit/hash.h"
#include "rekit/zip.h"

namespace rekit {
namespace {
uint32_t le16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
}  // namespace

// The layout as unshield (github.com/twogood/unshield, lib/) reads version 5:
// the common header, the cab descriptor's file table (directories, then file
// descriptors of 0x3a bytes), each compressed file as chunks of a 16-bit
// length and raw deflate, its expanded bytes' MD5 at the descriptor's +0x2a.
bool each_installshield(const std::vector<uint8_t>& hdr, uint64_t cab_size, const CabRead& read,
                        const std::function<bool(CabFile&&)>& each, std::string* error) {
  auto fail = [&](const std::string& why) {
    *error = why;
    return false;
  };
  if (hdr.size() < 20 || le32(hdr.data()) != 0x28635349) return fail("not an InstallShield header");
  const uint32_t version = le32(&hdr[4]);
  const uint32_t major = (version >> 24) == 1 ? (version >> 12) & 0xf : 0;
  if (major != 5) return fail("InstallShield version " + std::to_string(major) + ", not 5 (version word " + std::to_string(version) + ")");
  const size_t desc = le32(&hdr[12]);
  if (desc + 0x30 > hdr.size()) return fail("cab descriptor out of the header");
  const size_t table = desc + le32(&hdr[desc + 0x0c]);
  const uint32_t dirs = le32(&hdr[desc + 0x1c]), files = le32(&hdr[desc + 0x28]);
  if (table + 4ull * (dirs + files) > hdr.size()) return fail("file table out of the header");
  auto str = [&](size_t at) {
    std::string s;
    while (at < hdr.size() && hdr[at]) s += static_cast<char>(hdr[at++]);
    return s;
  };
  for (uint32_t i = 0; i < files; ++i) {
    const size_t at = table + le32(&hdr[table + 4 * (dirs + i)]);
    if (at + 0x3a > hdr.size()) return fail("file descriptor out of the header");
    const uint8_t* d = &hdr[at];
    const uint32_t name = le32(d), dir = le32(d + 4), flags = le16(d + 8);
    const uint32_t expanded = le32(d + 10), compressed = le32(d + 14), offset = le32(d + 0x26);
    if ((flags & 8) || !name || !offset || dir >= dirs) continue;  // invalid: no data
    std::string path = str(table + le32(&hdr[table + 4 * dir]));
    path = (path.empty() ? "" : path + "/") + str(table + name);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (flags & 1) return fail(path + " is split across volumes");
    if (flags & 2) return fail(path + " is obfuscated (not supported)");
    const size_t stored = flags & 4 ? compressed : expanded;
    if (offset + static_cast<uint64_t>(stored) > cab_size) return fail(path + ": data past the cabinet's end");
    std::vector<uint8_t> raw(stored);
    if (stored && !read(offset, stored, raw.data())) return fail(path + ": cannot be read from the cabinet");
    CabFile e{path, {}};
    if (flags & 4) {
      e.data.reserve(expanded);
      for (size_t k = 0; k < raw.size();) {
        const size_t len = k + 2 <= raw.size() ? le16(&raw[k]) : 0;
        if (!len || k + 2 + len > raw.size()) return fail(path + ": bad chunk length");
        std::vector<uint8_t> chunk;
        if (!inflate_raw(&raw[k + 2], len, &chunk, 0)) return fail(path + ": does not inflate");
        e.data.insert(e.data.end(), chunk.begin(), chunk.end());
        k += 2 + len;
      }
    } else {
      e.data = std::move(raw);
    }
    if (e.data.size() != expanded) return fail(path + ": " + std::to_string(e.data.size()) + " bytes, not " + std::to_string(expanded));
    uint8_t sum[16];
    md5(e.data.data(), e.data.size(), sum);
    if (std::memcmp(sum, d + 0x2a, 16) != 0) return fail(path + ": MD5 does not match (a damaged cabinet?)");
    if (!each(std::move(e))) return fail(path + ": not taken");
  }
  return true;
}

bool list_installshield(const std::vector<uint8_t>& hdr, std::vector<std::pair<std::string, uint64_t>>* out,
                        std::string* error) {
  if (hdr.size() < 20 || le32(hdr.data()) != 0x28635349) return *error = "not an InstallShield header", false;
  const uint32_t version = le32(&hdr[4]);
  if ((version >> 24) != 1 || ((version >> 12) & 0xf) != 5) return *error = "not InstallShield 5", false;
  const size_t desc = le32(&hdr[12]);
  if (desc + 0x30 > hdr.size()) return *error = "cab descriptor out of the header", false;
  const size_t table = desc + le32(&hdr[desc + 0x0c]);
  const uint32_t dirs = le32(&hdr[desc + 0x1c]), files = le32(&hdr[desc + 0x28]);
  if (table + 4ull * (dirs + files) > hdr.size()) return *error = "file table out of the header", false;
  auto str = [&](size_t at) {
    std::string s;
    while (at < hdr.size() && hdr[at]) s += static_cast<char>(hdr[at++]);
    return s;
  };
  for (uint32_t i = 0; i < files; ++i) {
    const size_t at = table + le32(&hdr[table + 4 * (dirs + i)]);
    if (at + 0x3a > hdr.size()) return *error = "file descriptor out of the header", false;
    const uint8_t* d = &hdr[at];
    const uint32_t name = le32(d), dir = le32(d + 4), flags = le16(d + 8);
    if ((flags & 8) || !name || !le32(d + 0x26) || dir >= dirs) continue;
    std::string path = str(table + le32(&hdr[table + 4 * dir]));
    path = (path.empty() ? "" : path + "/") + str(table + name);
    std::replace(path.begin(), path.end(), '\\', '/');
    out->emplace_back(path, le32(d + 10));
  }
  return true;
}

bool read_installshield(const std::vector<uint8_t>& hdr, const std::vector<uint8_t>& cab, std::vector<CabFile>* out,
                        std::string* error) {
  return each_installshield(
      hdr, cab.size(),
      [&](uint64_t at, size_t n, uint8_t* to) {
        std::memcpy(to, cab.data() + at, n);
        return true;
      },
      [&](CabFile&& e) {
        out->push_back(std::move(e));
        return true;
      },
      error);
}

}  // namespace rekit
