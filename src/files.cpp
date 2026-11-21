#include "rekit/files.h"

#include <algorithm>
#include <cstdio>
#include <vector>
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

CopyResult copy_folder(const std::string& from, const std::string& to, const CopyProgress& progress) {
  CopyResult r;
  std::error_code ec;
  if (!fs::is_directory(from, ec)) return r.error = from + " is not a folder", r;
  if (fs::exists(to, ec)) return r.error = to + " already exists", r;
  if (path_inside(from, to) || path_inside(to, from)) return r.error = "cannot copy a folder into itself", r;
  // What is copied, and its size.
  struct Item {
    fs::path src;
    std::string rel;
    uint64_t size;
  };
  std::vector<Item> items;
  uint64_t total = 0;
  for (auto it = fs::recursive_directory_iterator(from, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it.depth() > 16 || items.size() > 200000) return r.error = from + " has too many files or folders", r;
    if (it->is_symlink(ec)) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec)) continue;
    const std::string rel = fs::relative(it->path(), from, ec).generic_string();
    if (ec || safe_relative(rel).empty()) continue;
    const uint64_t size = it->file_size(ec);
    if (ec) continue;
    items.push_back({it->path(), rel, size});
    total += size;
  }
  if (ec) return r.error = "cannot read " + from + ": " + ec.message(), r;
  fs::create_directories(fs::path(to).parent_path(), ec);
  ec.clear();
  if (!has_room(fs::path(to).parent_path().string(), static_cast<int64_t>(total), &r.needed, &r.free)) {
    r.no_space = true;
    r.error = "not enough free space";
    return r;
  }
  auto fail = [&](const std::string& why) {
    std::error_code e2;
    fs::remove_all(to, e2);
    r.ok = false;
    r.error = why;
    return r;
  };
  if (!fs::create_directories(to, ec) || ec) return fail("cannot make " + to);
  std::vector<char> buf(1 << 20);
  for (const Item& item : items) {
    if (progress && !progress(r.bytes, total, item.rel)) {
      r.cancelled = true;
      return fail("cancelled");
    }
    const fs::path dst = fs::path(to) / item.rel;
    fs::create_directories(dst.parent_path(), ec);
    std::FILE* in = std::fopen(item.src.string().c_str(), "rb");
    if (!in) return fail("cannot read " + item.src.string());
    std::FILE* out = std::fopen(dst.string().c_str(), "wb");
    if (!out) {
      std::fclose(in);
      return fail("cannot write " + dst.string());
    }
    bool good = true;
    uint64_t done_here = 0;
    for (size_t n; (n = std::fread(buf.data(), 1, buf.size(), in)) > 0;) {
      if (std::fwrite(buf.data(), 1, n, out) != n) {
        good = false;
        break;
      }
      done_here += n;
      if (progress && done_here % (8u << 20) < n && !progress(r.bytes + done_here, total, item.rel)) {
        std::fclose(in);
        std::fclose(out);
        r.cancelled = true;
        return fail("cancelled");
      }
    }
    good = good && !std::ferror(in);
    std::fclose(in);
    if (std::fclose(out) != 0) good = false;
    if (!good) return fail("cannot copy " + item.rel + " (is the disk full?)");
    r.bytes += done_here;
    ++r.files;
  }
  if (progress) progress(r.bytes, total, "");
  r.ok = true;
  return r;
}

}  // namespace rekit
