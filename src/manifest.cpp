#include "rekit/manifest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rekit/hash.h"

namespace fs = std::filesystem;

namespace rekit {
namespace {

bool hex64(const std::string& s) {
  if (s.size() != 64) return false;
  for (const char c : s)
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  return true;
}
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string file_name(const std::string& rel) {
  const size_t slash = rel.rfind('/');
  return slash == std::string::npos ? rel : rel.substr(slash + 1);
}

}  // namespace

bool write_manifest(const std::string& root, const std::vector<LeaveOut>& leave_out, const FilesProgress& progress,
                    std::string* error, bool* cancelled) {
  error->clear();
  if (cancelled) *cancelled = false;
  std::error_code ec;
  std::vector<std::string> files;
  for (fs::recursive_directory_iterator it(fs::path(root), fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->is_symlink(ec)) continue;
    const std::string rel = fs::relative(it->path(), fs::path(root), ec).generic_string();
    if (ec || rel.empty() || rel == kFilesManifest || rel == std::string(kFilesManifest) + ".part") continue;
    files.push_back(rel);
  }
  if (ec) return *error = "Could not list " + root + ": " + ec.message(), false;
  std::sort(files.begin(), files.end());
  std::string text;
  int done = 0;
  for (const std::string& rel : files) {
    const fs::path p = fs::path(root) / fs::path(rel);
    std::string sum;
    const auto hash = [&] { return sum.empty() ? (sum = sha256_file_hex(p.string())) : sum; };
    if (left_out(leave_out, file_name(rel), hash)) continue;
    if (hash().empty()) return *error = "Could not read " + rel, false;
    text += sum + "  " + rel + "\n";
    if (progress && !progress(++done, static_cast<int>(files.size()), rel)) {
      if (cancelled) *cancelled = true;
      return *error = "cancelled", false;
    }
  }
  const fs::path out = fs::path(root) / kFilesManifest, part = fs::path(root) / (std::string(kFilesManifest) + ".part");
  {
    std::ofstream f(part, std::ios::binary);
    f << text;
    if (!f.good()) {
      fs::remove(part, ec);
      return *error = "Could not write " + out.string(), false;
    }
  }
  fs::rename(part, out, ec);
  if (ec) {
    fs::remove(part, ec);
    return *error = "Could not write " + out.string(), false;
  }
  return true;
}

bool parse_manifest(const std::string& text, std::vector<ManifestEntry>* out) {
  out->clear();
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    // "<hex>  <path>" (sha256sum's text mode) or "<hex> *<path>" (its binary mode)
    if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) return out->clear(), false;
    const std::string sum = line.substr(0, 64), path = line.substr(66);
    if (!hex64(sum) || safe_relative(path) != path) return out->clear(), false;
    out->push_back({lower(sum), path});
  }
  return true;
}

FilesCheck check_files(const std::string& root, const std::vector<LeaveOut>& leave_out, const FilesProgress& progress,
                       const std::vector<LeaveOut>& rewritten) {
  FilesCheck r;
  const fs::path m = fs::path(root) / kFilesManifest;
  std::error_code ec;
  if (!fs::is_regular_file(m, ec)) return r;
  r.has_manifest = true;
  std::ifstream f(m, std::ios::binary);
  std::stringstream text;
  text << f.rdbuf();
  std::vector<ManifestEntry> list;
  if (!f.good() && !f.eof()) return r.error = "Could not read " + m.string(), r;
  if (!parse_manifest(text.str(), &list)) return r.error = m.string() + " is not a list of files and their sums", r;
  int done = 0;
  for (const ManifestEntry& e : list) {
    const fs::path p = fs::path(root) / fs::path(e.path);
    std::string sum;
    const auto hash = [&] { return sum.empty() ? (sum = sha256_file_hex(p.string())) : sum; };
    if (!left_out(leave_out, file_name(e.path), hash) && !left_out(rewritten, file_name(e.path), hash)) {
      if (!fs::is_regular_file(p, ec)) r.missing.push_back(e.path);
      else if (hash() != e.sha256) r.changed.push_back(e.path);
      else ++r.checked;
    }
    if (progress && !progress(++done, static_cast<int>(list.size()), e.path)) return r.cancelled = true, r;
  }
  return r;
}

}  // namespace rekit
