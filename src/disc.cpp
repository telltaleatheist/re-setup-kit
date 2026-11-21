#include "rekit/disc.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "rekit/cue_audio.h"

namespace rekit {
namespace fs = std::filesystem;
namespace {

constexpr uint32_t kUser = 2048;  // an ISO 9660 sector's user data

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
bool ends_with(const std::string& s, const std::string& e) {
  return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

}  // namespace

Disc::~Disc() {
  if (image_) std::fclose(image_);
}

std::string Disc::kind() const {
  if (!root_.empty()) return "folder";
  return sector_ == kUser ? "ISO image" : "raw image (" + std::to_string(sector_) + "-byte sectors)";
}

const Disc::File* Disc::find(const std::string& path) const {
  const std::string p = lower(path);
  for (const File& f : files_)
    if (lower(f.path) == p) return &f;
  return nullptr;
}

bool Disc::read_sectors(uint64_t sector, uint32_t in, size_t n, uint8_t* out) {
  while (n) {
    const size_t take = std::min<size_t>(n, kUser - in);
    const int64_t at = start_ + static_cast<int64_t>(sector) * sector_ + user_ + in;
    if (std::fseek(image_, static_cast<long>(at), SEEK_SET) != 0 || std::fread(out, 1, take, image_) != take) return false;
    out += take, n -= take, ++sector, in = 0;
  }
  return true;
}

bool Disc::read(const File& f, uint64_t off, size_t n, uint8_t* out) {
  if (off + n > f.size) return false;
  if (!root_.empty()) {
    std::FILE* h = std::fopen((fs::path(root_) / f.path).string().c_str(), "rb");
    const bool ok = h && std::fseek(h, static_cast<long>(off), SEEK_SET) == 0 && std::fread(out, 1, n, h) == n;
    if (h) std::fclose(h);
    return ok;
  }
  return read_sectors(f.at + off / kUser, static_cast<uint32_t>(off % kUser), n, out);
}

// An ISO 9660 directory's records (ECMA-119 9.1): a record never crosses a
// sector, a zero length byte ends the sector's records; "\0" and "\1" are
// the directory itself and its parent, bit 1 of the flags a directory.
bool Disc::read_image_dir(uint64_t sector, uint64_t size, const std::string& prefix, int depth) {
  if (depth > 8 || size > (16u << 20) || files_.size() > 100000) return false;
  std::vector<uint8_t> d(static_cast<size_t>(size));
  if (!read_sectors(sector, 0, d.size(), d.data())) return false;
  for (size_t i = 0; i < d.size();) {
    const uint8_t len = d[i];
    if (len == 0) {
      i = (i / kUser + 1) * kUser;
      continue;
    }
    if (len < 34 || i + len > d.size()) return false;
    const uint8_t* r = &d[i];
    const uint8_t nlen = r[32];
    if (33u + nlen > len) return false;
    std::string name(reinterpret_cast<const char*>(r + 33), nlen);
    i += len;
    if (nlen == 1 && (name[0] == 0 || name[0] == 1)) continue;
    if (const size_t semi = name.find(';'); semi != std::string::npos) name.resize(semi);
    if (!name.empty() && name.back() == '.') name.pop_back();
    const uint64_t at = le32(r + 2), bytes = le32(r + 10);
    const std::string path = prefix.empty() ? name : prefix + "/" + name;
    if (r[25] & 2) {
      if (!read_image_dir(at, bytes, path, depth + 1)) return false;
    } else {
      files_.push_back({path, bytes, at});
    }
  }
  return true;
}

bool Disc::open(const std::string& path, std::string* error) {
  std::error_code ec;
  files_.clear();
  if (fs::is_directory(path, ec)) {
    root_ = path;
    int seen = 0;
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator() && seen < 100000; it.increment(ec), ++seen) {
      if (ec) break;
      if (it.depth() > 8) it.disable_recursion_pending();
      if (!it->is_regular_file(ec)) continue;
      files_.push_back({fs::relative(it->path(), path, ec).generic_string(), static_cast<uint64_t>(it->file_size(ec)), 0});
    }
    return true;
  }
  std::string image = path;
  int64_t start = -1;
  if (ends_with(lower(path), ".cue")) {
    // The first data track (Redump's discs: track 1, MODE1/2352).
    std::vector<CueTrack> tracks;
    if (!read_cue(path, &tracks, error)) return false;
    auto t = std::find_if(tracks.begin(), tracks.end(), [](const CueTrack& c) { return !c.audio; });
    if (t == tracks.end()) return *error = path + ": no data track", false;
    const std::string mode = upper(t->mode);
    if (mode == "MODE1/2352") sector_ = 2352, user_ = 16;
    else if (mode == "MODE2/2352") sector_ = 2352, user_ = 24;  // XA form 1: the subheader after the header
    else if (mode == "MODE1/2048") sector_ = kUser, user_ = 0;
    else return *error = path + ": data track " + t->mode + " (MODE1/2352, MODE2/2352 or MODE1/2048 read)", false;
    image = (fs::path(path).parent_path() / t->file).string();
    start = t->index1 * sector_;
    cue = path;
  }
  image_ = std::fopen(image.c_str(), "rb");
  if (!image_) return *error = "cannot open " + image, false;
  if (start < 0) {
    // A bare image: raw sectors start with the 12-byte sync pattern.
    uint8_t h[16] = {};
    const bool raw = std::fread(h, 1, 16, image_) == 16 && h[0] == 0 && h[11] == 0 &&
                     std::all_of(h + 1, h + 11, [](uint8_t b) { return b == 0xff; });
    sector_ = raw ? 2352 : kUser;
    user_ = raw ? (h[15] == 2 ? 24 : 16) : 0;
    start = 0;
  }
  start_ = start;
  // The primary volume descriptor at sector 16: type 1, "CD001"; its root
  // directory's record at +156.
  uint8_t pvd[kUser];
  if (!read_sectors(16, 0, kUser, pvd) || pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0)
    return *error = image + ": no ISO 9660 file system (not a data disc image?)", false;
  if (!read_image_dir(le32(pvd + 156 + 2), le32(pvd + 156 + 10), "", 0))
    return *error = image + ": a damaged directory", false;
  return true;
}

}  // namespace rekit
