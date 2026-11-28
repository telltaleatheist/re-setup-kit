#pragma once
// A record of installed files and their sums, for the in-game menu's CHECK MY FILES (docs/plan-in-game-menu.md
// section 3, step 4; 2026-11-27). The file is `rebuild-files.sha256` at the top of the files it covers, in
// sha256sum's format ("<64 hex>  <relative/path>" a line, '/' between folders), so `sha256sum -c` reads it too.
// The kit writes one for a game's files after an install; a game's package carries one for its program
// (tools/files_manifest.py writes the same).
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "rekit/files.h"

namespace rekit {

inline constexpr const char* kFilesManifest = "rebuild-files.sha256";

struct ManifestEntry {
  std::string sha256;  // lower case
  std::string path;    // relative, '/' between folders
};

// Each file done (its relative path), out of `total`; false cancels.
using FilesProgress = std::function<bool(int done, int total, const std::string& path)>;

// Every regular file under `root` (but the manifest itself and what `leave_out` names) hashed into `root`/kFilesManifest,
// sorted by path, written whole or not at all. False with `error` (or cancelled).
bool write_manifest(const std::string& root, const std::vector<LeaveOut>& leave_out, const FilesProgress& progress,
                    std::string* error, bool* cancelled = nullptr);

// A manifest's lines; false when a line is not one (nothing kept then).
bool parse_manifest(const std::string& text, std::vector<ManifestEntry>* out);

// `root` checked against its manifest. A file the manifest names but `leave_out` lists is not looked at (left out on
// purpose is not damage); a file the manifest does not name (a mod, a save) is not either. Nor is one `rewritten`
// names (matched as leave_out is): a file the game itself rewrites in its own folder (an original's config file, a
// high-score table), listed in the record but never compared, changed or gone.
struct FilesCheck {
  bool has_manifest = false;  // false: no record to check against (nothing else is set)
  bool cancelled = false;
  int checked = 0;                    // files read and compared
  std::vector<std::string> missing;   // named, not there
  std::vector<std::string> changed;   // there, a different sum (or unreadable)
  std::string error;                  // the manifest could not be read
  bool ok() const { return has_manifest && !cancelled && error.empty() && missing.empty() && changed.empty(); }
};
FilesCheck check_files(const std::string& root, const std::vector<LeaveOut>& leave_out, const FilesProgress& progress,
                       const std::vector<LeaveOut>& rewritten = {});

}  // namespace rekit
