#include "rekit/files.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace rekit {
namespace {
uint64_t g_spare = 64u << 20;
}  // namespace

uint64_t& space_spare() { return g_spare; }

std::string safe_relative(const std::string& entry) {
  std::string out;
  size_t at = 0;
  std::string p = entry;
  std::replace(p.begin(), p.end(), '\\', '/');
  if (p.empty() || p[0] == '/' || (p.size() >= 2 && p[1] == ':')) return "";  // absolute, a drive, a share
  while (at <= p.size()) {
    const size_t end = std::min(p.find('/', at), p.size());
    const std::string part = p.substr(at, end - at);
    at = end + 1;
    if (part.empty() || part == ".") continue;
    if (part == "..") return "";
    out += (out.empty() ? "" : "/") + part;
  }
  return out;
}

bool path_inside(const std::string& base, const std::string& p) {
  std::error_code ec;
  const fs::path b = fs::weakly_canonical(fs::path(base), ec), q = fs::weakly_canonical(fs::path(p), ec);
  if (ec) return false;
  const fs::path rel = q.lexically_relative(b);
  return !rel.empty() && rel != "." && *rel.begin() != "..";
}

bool has_room(const std::string& dir, int64_t size, uint64_t* needed, uint64_t* free) {
  std::error_code ec;
  const fs::space_info sp = fs::space(fs::path(dir), ec);
  if (ec || size < 0 || sp.available >= static_cast<uint64_t>(size) + g_spare) return true;
  if (needed) *needed = static_cast<uint64_t>(size) + g_spare;
  if (free) *free = sp.available;
  return false;
}

bool swap_in_folder(const std::string& tmp, const std::string& out, const std::string& marker, std::error_code& ec) {
  const fs::path old = out + ".old";
  std::error_code e2;
  const bool had = fs::exists(out, e2);
  if (had && !fs::exists(fs::path(out) / marker, e2) && !fs::is_empty(out, e2)) {
    ec = std::make_error_code(std::errc::directory_not_empty);
    return false;
  }
  if (fs::exists(old / marker, e2)) fs::remove_all(old, e2);
  if (had) {
    fs::rename(out, old, ec);
    if (ec) return false;
  }
  fs::create_directories(fs::path(out).parent_path(), e2);
  fs::rename(tmp, out, ec);
  if (ec) {
    if (had) fs::rename(old, out, e2);  // the old one back
    return false;
  }
  fs::remove_all(old, e2);
  return true;
}

}  // namespace rekit
