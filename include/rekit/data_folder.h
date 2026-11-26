#pragma once
// A game's user folder (its settings, pilots, saves, converted movies) elsewhere than the usual place, on another
// drive say (from mw3-re's mech3/data_folder, 2026-11-25). A pointer file in the usual place names it, and a marker in
// the folder says it is one of ours. Moving copies, verifies, switches the pointer, and only then may remove the old
// copy, file by file: nothing here removes a folder's contents it did not itself copy or make. The files' names are
// the ones the -re games have always written, so every version reads the same folders.
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rekit::data_folder {

constexpr const char* kPointer = "data-folder.txt";       // in the usual place
constexpr const char* kMarker = "mw3-data-folder.txt";     // in a folder the pointer may name (the family's first name)
constexpr const char* kCopying = "mw3-data-copying.txt";   // in a copy not finished yet

// What a game tells these about itself.
struct Game {
  std::string title;                 // its name in the sentences: "Your <title> data folder is not there: ..."
  std::vector<std::string> configs;  // its settings files in the folder, whose saved paths follow a move ("game.cfg")
  // Destinations in plain ASCII only (Windows before 10 1903, whose narrow calls read another code page).
  bool plain_names_only = false;
  // A saved path as text read back (the pointer's line): an earlier build may have written it in the system's code
  // page. Null: as written.
  std::function<std::string(const std::string&)> path_from_text;
  std::string log_prefix;  // before the lines these print ("game: ")
};

// The folder to use. No pointer: `usual`, as if none of this existed. A pointer whose folder cannot be used (drive not
// connected, read-only, full): `error` says so and `dir` is that folder, never a fresh one in its place.
struct Resolved {
  std::string dir;
  bool pointer = false;
  std::string error;
};
Resolved resolve(const std::string& usual, const Game& game);

// "" if the folder can be used: it is there, it has the marker, a small write works.
std::string unusable(const std::string& dir, const Game& game);

// Where a picked folder means to move to: itself if it is empty or holds no data of ours, else a `name` folder inside
// it (a drive's root): "<Game> Data".
std::string destination(const std::string& picked, const std::string& usual, const std::string& name);

// A move's check before starting: `error` if it cannot be done.
struct Plan {
  uint64_t bytes = 0, free = 0;
  int files = 0;
  std::string error;
};
Plan plan(const std::string& from, const std::string& to, const std::string& usual, const Game& game);

struct Moved {
  bool ok = false, cancelled = false;
  std::string error;
  std::vector<std::string> copied;  // relative: what remove_old may remove in `from`
  uint64_t bytes = 0;
};
// The copy, each file read back and compared; the game's settings files' paths under `from` now under `to`; the
// pointer switched last. On failure or cancel what this made is taken away again and the pointer is as it was. The log
// files stay behind: they are in use, and each start makes new ones.
Moved move_data(const std::string& from, const std::string& to, const std::string& usual, const Game& game,
                const std::function<bool(double, const std::string&)>& progress);

struct Removed {
  int files = 0;
  std::vector<std::string> failed;
  std::string error;  // nothing was removed
};
// The player's yes after a move: the files move_data copied, only when the pointer names `to` and `to` still has every
// one. Folders only when empty.
Removed remove_old(const std::string& from, const std::string& to, const std::vector<std::string>& copied,
                   const std::string& usual, const Game& game);

// Only plain ASCII in the path.
bool plain_name(const std::string& path);

// What is not the player's data at a folder's top: the pointer, the marker and the copying flag, logs, and what a file
// manager leaves (.DS_Store, Thumbs.db, desktop.ini).
bool not_data(const std::string& name);
// Any of the player's data in the folder; a folder that cannot be listed may hold some.
bool holds_data(const std::string& dir);

// Tests: a drive's free bytes, when set; a call right after the pointer switches.
const uint64_t*& free_for_test();
std::function<void()>& after_switch_for_test();

// `path` under `from` as the same place under `to`; else as it was.
std::string moved_path(const std::string& path, const std::string& from, const std::string& to);

// `cfg`'s saved paths (key=value lines) under each moved pair's first folder made to name the same place under its
// second. A missing file is left missing.
void retarget_config(const std::string& cfg, const std::vector<std::pair<std::string, std::string>>& moved);

// The family folder (family_folder.h): `usual` is <base>/BattleTech/<Game>; a player's <base>/MicroProse/<Game> from
// before is renamed to it once (move_once), a pointer left in the old place so an earlier version of any of the games
// still finds the same data, and the saved paths in every family folder's settings files (`game.configs`) that named a
// moved folder made to name it where it is now. Returns the folder to use: `usual`, or the old one while it cannot be
// moved (a file in use: tried again at the next start). `move` false (a development build, on the developer's own
// folders): nothing moved, the old folder used while it is the one with the data.
std::string family_move(const std::string& usual, const Game& game, bool move = true);

}  // namespace rekit::data_folder
