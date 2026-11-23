#include "rekit/family_folder.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace rekit {

std::string old_org_folder(const std::string& folder) {
  fs::path p = fs::path(folder);
  while (!p.empty() && p.filename().empty()) p = p.parent_path();  // a trailing separator
  const fs::path org = p.parent_path();
  if (org.filename() != kFamilyOrg) return {};
  return (org.parent_path() / kOldOrg / p.filename()).string();
}

FolderMove move_once(const std::string& from, const std::string& to, const std::vector<std::string>& leftovers) {
  FolderMove m;
  std::error_code ec;
  if (from.empty() || to.empty() || !fs::is_directory(from, ec)) return m;
  bool ours = false;  // something in `from` besides what a move leaves
  for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    if (name != ".DS_Store" && std::find(leftovers.begin(), leftovers.end(), name) == leftovers.end()) ours = true;
  }
  if (!ours) return m;
  if (fs::exists(to, ec)) {
    if (!fs::is_directory(to, ec) || !fs::is_empty(to, ec)) return m;  // the new one in use already: never merged
    fs::remove(to, ec);
    if (ec) return m.kind = FolderMove::kFailed, m.error = "cannot replace the empty " + to + ": " + ec.message(), m;
  }
  fs::create_directories(fs::path(to).parent_path(), ec);
  fs::rename(from, to, ec);
  if (ec) {
    fs::create_directories(to, ec);  // as SDL_GetPrefPath left it
    return m.kind = FolderMove::kFailed, m.error = "cannot move " + from + " to " + to, m;
  }
  m.kind = FolderMove::kMoved;
  return m;
}

void remove_if_empty(const std::string& dir, int levels) {
  fs::path p = fs::path(dir);
  for (int i = 0; i < levels && !p.empty(); ++i, p = p.parent_path()) {
    std::error_code ec;
    if (!fs::is_directory(p, ec) || !fs::is_empty(p, ec)) return;
    fs::remove(p, ec);  // an empty folder only
    if (ec) return;
  }
}

}  // namespace rekit
