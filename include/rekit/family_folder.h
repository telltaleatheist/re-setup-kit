#pragma once
#include <string>
#include <vector>

// The family's user folders (Owen, 2026-11-22): every game's pilots, saves and settings under
// "BattleTech" (SDL_GetPrefPath("BattleTech", "<Game>")), no longer "MicroProse", since the games come from
// different studios. A player's folder from before is taken across once, renamed on the same drive: nothing
// is copied and nothing is merged.
namespace rekit {

constexpr const char* kFamilyOrg = "BattleTech";
constexpr const char* kOldOrg = "MicroProse";

// The same game's folder under the old name: <base>/MicroProse/<Game> for <base>/BattleTech/<Game>; "" when
// `folder` is not under BattleTech.
std::string old_org_folder(const std::string& folder);

struct FolderMove {
  enum Kind { kNothing, kMoved, kFailed } kind = kNothing;
  std::string error;  // kFailed: why; the old folder is still whole and in use
};
// `from` renamed to `to` when `from` holds something besides `leftovers` (names an earlier move left there,
// such as a pointer back, and the system's .DS_Store), and `to` is missing or empty (an empty `to` is removed
// first: SDL_GetPrefPath makes it). Never over a folder with something in it, never a copy. kFailed (another
// drive, a file in use): keep using `from`, and try again at the next start.
FolderMove move_once(const std::string& from, const std::string& to, const std::vector<std::string>& leftovers = {});

// `dir` removed when it is an empty folder, then its parent the same way, up to `levels` folders: what a move
// leaves behind. Never a folder with anything in it.
void remove_if_empty(const std::string& dir, int levels = 1);

}  // namespace rekit
