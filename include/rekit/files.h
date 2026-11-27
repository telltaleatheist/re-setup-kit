#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <system_error>
#include <vector>

// Writing an install safely (from mw3-re's source_install and disc_install): archive paths that cannot leave the
// folder, free space checked before each write, and a new install swapped in whole, replacing only a folder this
// kit made (its marker file) or an empty one, never anything else the player named.
namespace rekit {

// An entry's path made safe to write under a folder ('\\' read as '/', "." parts dropped); "" for an absolute
// path, a drive, a share, or any ".." part.
std::string safe_relative(const std::string& entry);

// Whether `p` resolves to a place inside `base` (links followed); `base` itself is not inside.
bool path_inside(const std::string& base, const std::string& p);

// The free space kept beside what is written (64 MB by default; tests raise it).
uint64_t& space_spare();

// Whether `dir`'s disk has `size` bytes plus the spare free. On false, `needed` and `free` say how much (a size
// below zero, unknown, or a disk that cannot be asked counts as room).
bool has_room(const std::string& dir, int64_t size, uint64_t* needed = nullptr, uint64_t* free = nullptr);

// `tmp` (a finished install) put in `out`'s place: `out` moved to `out`.old, `tmp` renamed in, the old removed
// last; on a failure the old comes back. `out` is replaced only when it holds `marker` (a file our install wrote)
// or is empty; anything else fails with directory_not_empty and is left as it was.
bool swap_in_folder(const std::string& tmp, const std::string& out, const std::string& marker, std::error_code& ec);

// A folder's regular files copied into `to` (made here; a folder already there is refused), keeping their
// relative paths: links, devices and anything that cannot be read as a file are skipped, as an installed game
// never needs them. The free space is checked first. A failure or a cancel removes `to`, so nothing half-copied
// is left. `to` may not be inside `from` (nor `from` inside `to`).
struct CopyResult {
  bool ok = false, cancelled = false, no_space = false;
  int files = 0;
  uint64_t bytes = 0;                 // copied
  uint64_t needed = 0, free = 0;      // no_space: on the disk that holds `to`
  std::string error;                  // why not, in plain words
};
// The bytes copied so far of all, and the file being copied; false cancels.
using CopyProgress = std::function<bool(uint64_t done, uint64_t total, const std::string& file)>;

// A file a game's install leaves out though it sits among the game's files (a download's added tool, such as a
// copy-protection remover beside the program): its name, matched without case against the file's own name in any
// folder, and optionally its SHA-256 (lowercase hex) so that only that exact file is left out. Optional: a game names
// none and everything is copied as before.
struct LeaveOut {
  std::string name;    // "NEV_UNIV.EXE"
  std::string sha256;  // "" : any file of that name
};
// Whether a file named `name` (its own name, no folder) is on `list`; `sha256` is asked only when an entry with that
// name gives one (the file's hash, read lazily).
bool left_out(const std::vector<LeaveOut>& list, const std::string& name, const std::function<std::string()>& sha256);

CopyResult copy_folder(const std::string& from, const std::string& to, const CopyProgress& progress = nullptr,
                       const std::vector<LeaveOut>& leave_out = {});

}  // namespace rekit
