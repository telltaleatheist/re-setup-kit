#include "rekit/backup.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "rekit/files.h"

namespace fs = std::filesystem;

namespace rekit {
namespace {

std::string now_stamp() {
  const std::time_t now = std::time(nullptr);
  std::tm t{};
#if defined(_WIN32)
  localtime_s(&t, &now);
#else
  localtime_r(&now, &t);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H.%M.%S", &t);
  return buf;
}

constexpr size_t kStampLength = 19;  // "YYYY-MM-DD HH.MM.SS"

}  // namespace

Backup back_up(const std::string& user_folder, const std::vector<std::string>& paths, const std::string& what, int keep) {
  Backup b;
  std::error_code ec;
  const fs::path base = fs::path(user_folder) / "Backups";
  fs::create_directories(base, ec);
  if (ec) return b.error = "the Backups folder could not be made (" + ec.message() + ")", b;
  const std::string name = now_stamp() + " " + what;
  fs::path dir = base / name;
  for (int n = 2; fs::exists(fs::symlink_status(dir, ec)) || fs::exists(fs::symlink_status(dir.string() + ".part", ec)); ++n)
    dir = base / (name + " (" + std::to_string(n) + ")");
  const fs::path part = dir.string() + ".part";
  fs::create_directories(part, ec);
  if (ec) return b.error = "the backup's folder could not be made (" + ec.message() + ")", b;
  const auto fail = [&](const std::string& why) {
    fs::remove_all(part, ec);
    b.error = why;
    return b;
  };
  const fs::path home = fs::weakly_canonical(user_folder, ec);
  for (const std::string& p : paths) {
    fs::path from(p);
    if (from.is_relative()) from = fs::path(user_folder) / from;
    const fs::file_status st = fs::symlink_status(from, ec);
    if (!fs::exists(st)) continue;
    // Its place under user_folder when it is inside it, else its own name.
    const fs::path full = fs::weakly_canonical(from, ec);
    std::string rel = path_inside(home.string(), full.string()) ? fs::relative(full, home, ec).generic_string() : std::string();
    if (rel.empty() || rel == ".") rel = from.filename().string();
    rel = safe_relative(rel);
    if (rel.empty()) continue;
    const fs::path to = part / rel;
    fs::create_directories(to.parent_path(), ec);
    if (fs::is_directory(st)) {
      const CopyResult r = copy_folder(from.string(), to.string());
      if (!r.ok) return fail(p + " could not be copied (" + r.error + ")");
    } else if (fs::is_regular_file(st)) {
      fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
      if (ec) return fail(p + " could not be copied (" + ec.message() + ")");
    }
  }
  {
    std::ofstream mark(part / kBackupMark, std::ios::binary);
    mark << "# A backup the game made (" << what << "); the oldest of its kind are removed as new ones come.\nwhat=" << what << "\n";
    if (!mark) return fail("the backup could not be written");
  }
  fs::rename(part, dir, ec);
  if (ec) return fail("the backup could not be finished (" + ec.message() + ")");
  b.ok = true, b.folder = dir.make_preferred().string();
  // The oldest of the same `what` past `keep`, the kit's own only (its mark inside). Names sort by their stamp.
  if (keep > 0) {
    std::vector<fs::path> same;
    for (const auto& e : fs::directory_iterator(base, ec)) {
      const std::string n = e.path().filename().string();
      if (!e.is_directory(ec) || n.size() <= kStampLength + 1 || n.compare(kStampLength + 1, what.size(), what) != 0) continue;
      const std::string tail = n.substr(kStampLength + 1 + what.size());
      if (!tail.empty() && tail.rfind(" (", 0) != 0) continue;  // another `what` that starts with this one
      if (fs::is_regular_file(e.path() / kBackupMark, ec)) same.push_back(e.path());
    }
    std::sort(same.begin(), same.end());
    for (size_t i = 0; i + static_cast<size_t>(keep) < same.size(); ++i) fs::remove_all(same[i], ec);
  }
  return b;
}

}  // namespace rekit
