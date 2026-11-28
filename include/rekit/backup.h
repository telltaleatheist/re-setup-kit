#pragma once
// The player's files copied before something changes them (2026-11-27, docs/plan-in-game-menu.md section 3): UPDATE
// NOW backs up saves, pilots and settings, RESET SETTINGS the settings. One helper for both (dev 1's shape).
#include <string>
#include <vector>

namespace rekit {

constexpr const char* kBackupMark = "rebuild-backup.txt";

struct Backup {
  bool ok = false;
  std::string folder, error;  // ok: the backup's folder; else why, in plain words
};
// Each of `paths` (a file or a folder; a missing one passed over) copied into
// <user_folder>/Backups/<YYYY-MM-DD HH.MM.SS> <what>/: a path inside `user_folder` keeps its place there, any other
// goes in by its own name. Written as <folder>.part and renamed when whole, so a failure leaves nothing. Then the
// oldest backups of the same `what` past `keep` are removed: only folders holding kBackupMark (the kit's own), never
// another folder in Backups. `keep` <= 0 removes none.
Backup back_up(const std::string& user_folder, const std::vector<std::string>& paths, const std::string& what, int keep);

}  // namespace rekit
