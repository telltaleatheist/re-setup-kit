#pragma once
// A game updating itself (docs/plan-in-game-menu.md sections 3 and 4): the check against the signed catalog's homes,
// the download (signed SHA256SUMS only), the backup, the swap by install type and the restart, the start-up check and
// the update count. The logic is rekit/update.h; this half fetches with the system's curl (setup_original.h) and
// starts processes. The kit names no game: the game's UpdateSource says what to ask and what to replace.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rekit/sdl/setup_screen.h"
#include "rekit/update.h"

namespace rekit::sdl {

struct UpdateSource {
  UpdateRecipe recipe;                 // repo, tags, this program's tag and the release file it updates from
  std::vector<std::string> catalogs;   // the signed catalog's addresses, asked in order (the setup screen's)
  std::vector<std::string> keys;       // the keys the catalog and each release's SHA256SUMS must be signed by
  // What an update replaces; "": found from this program's path (self_path): kMacApp the .app holding it, kAppImage
  // the AppImage, kFolder the program's folder. Windows Setup needs none (the Setup finds its install).
  std::string target;
  // kFolder: a path inside the folder that marks it the game's ("mw3.exe"); "": the program's own name.
  std::string marker;
  // In the user folder, what is backed up before anything changes (saves, pilots, settings); the newest kBackupsKept.
  std::vector<std::string> keep;
  std::vector<std::string> restart_args;  // the restarted program's arguments
  // --- For checks.
  std::string temp;          // the downloads' folder's parent; "": the system's temp folder
  bool allow_file = false;   // file:/// catalogs, homes and files
  bool no_restart = false;   // swap, but start nothing
  std::function<int64_t()> now_s;  // the clock for the once-a-day check; null: the system's
  bool usable() const { return !recipe.repo.empty() && !catalogs.empty() && !keys.empty(); }
};
constexpr int kBackupsKept = 5;
constexpr const char* kUpdateMark = "rekit-update-download.txt";
constexpr const char* kCountAgent = "game";  // the count's launcher= when a game updated itself

// RE_LAUNCHER is set: the launcher started this program (it checks for updates itself; no start-up prompt).
bool launched_by_launcher();

// <temp>/rekit-update-<key>, made and marked as the kit's; "" with `error` when that name holds someone else's folder.
std::string update_dir(const std::string& temp, const std::string& key, std::string* error);

// The signed catalog (the first of `catalogs` signed by one of `keys`), its homes asked in order for the recipe's
// releases, judged (judge_update). `counts`: the catalog's "counts" ("" none). Each fetch is capped (20 s), so an
// offline or slow start never waits long; `go_on` false stops it (the game quitting). Never a network request in a
// test run (RE_KIT_TEST).
UpdateCheck check_update(const UpdateSource& src, std::string* counts, const std::function<bool()>& go_on);

// The newest release's file from the check's sources in order, each source's SHA256SUMS fetched with its .sig and
// checked against src.keys first, then the file (resumed after a dropped connection) checked against its line.
// `file` the download's path in `dir`, `github` whether it came from a home keyed "github" (the count's src).
bool download_update(const UpdateSource& src, const UpdateCheck& check, const std::string& dir, const ProgressFn& progress,
                     std::string* file, bool* github, std::string* error, bool* cancelled);

// Why this copy can't update itself ("" when it can): an unknown or development install, a copy the launcher installed
// (it updates it), a Mac app run translocated, or a folder this user can't change (where the swap writes beside the
// program: a Mac app in an admin's Applications). The UPDATES page then offers the release page instead.
std::string update_blocked(const UpdateSource& src, InstallKind kind);

// What install_update did.
struct UpdateInstall {
  bool quit = false;    // true: the game quits now (the swap and the restart are done or waiting for it to go)
  std::string backup;   // the backup's folder ("" when nothing was backed up)
  std::string words;    // for the page and the log: why not, or what happens next
};
// UPDATE NOW, after the player agreed to close the game: the player's files backed up (back_up, src.keep), the
// update note written (the next start counts it), then by install kind: Windows Setup, a hidden helper script run
// that waits for this process to end, runs the Setup silently in the install's scope and restarts the game; Windows
// folder, the same helper moving the unpacked folder in; elsewhere the swap done now (swap_in: the AppImage, the
// folder, the Mac app from the release's disk image) and a helper that restarts the game once this one is gone.
// Nothing changes for an unknown or development install, a copy the launcher installed (it updates it), or a Mac app
// run translocated (it says to move the app to Applications).
UpdateInstall install_update(const UpdateSource& src, InstallKind kind, const std::string& user_folder, const std::string& file,
                             bool github, const std::string& to, const std::string& counts);

// A start: the update note an earlier UPDATE NOW left, taken, and its count sent when this program is the version it
// installed (blocking, capped at 10 s, `go_on` false stops it; nothing in a test run).
void report_finished_update(const UpdateSource& src, const std::string& user_folder, const std::function<bool()>& go_on = nullptr);

// The start-up check (plan section 4) on its own thread: the finished update's count, then, unless the launcher
// started this program or the player unticked "Check for updates when <game> starts" or one ran in the last day, one
// check. Offline or slow, it says nothing. The destructor stops it (no wait for a slow server).
class StartUpdateCheck {
 public:
  StartUpdateCheck(UpdateSource src, std::string user_folder);
  ~StartUpdateCheck();
  StartUpdateCheck(const StartUpdateCheck&) = delete;
  StartUpdateCheck& operator=(const StartUpdateCheck&) = delete;
  // Once, when it finished: the check (kNewer means ask the player) and the catalog's counts.
  bool take(UpdateCheck* check, std::string* counts);
  bool running() const;

  struct State;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace rekit::sdl
