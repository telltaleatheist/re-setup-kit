#pragma once
// The setup screen (DESIGN.md step 5; docs/plan-in-game-menu.md section 1): a game started on its own with its files
// missing, or asked `--setup`, opens it. The kit names no game: the words are the kit's with the game's title filled
// in, and the look is the game's theme over the launcher's (Theme). It returns the launcher contract's exit.
//
// A fresh setup (no files in use, nothing to look at first) shows no menu the usual way (2026-11-27): a copy found on
// this computer is used at once, else the catalog's original files download at once, each in a small window (what
// is coming, its size, the progress, CANCEL and USE MY OWN COPY INSTEAD); the game's files in, it ends ready. A look or
// download that fails or is cancelled, the link, or no source turn it into the setup window, on GAME FILES: FOUND ON
// THIS COMPUTER (USE THIS), USE MY OWN COPY (PICK A FILE, PICK A FOLDER, SEARCH AGAIN) and DOWNLOAD THEM. Choosing
// again (files in use), a find to look at first and REKIT_SETUP_FIND open the setup window at once. It is always an
// ordinary window, never fullscreen, so the system's pickers behave.
//
// In a run no player is at (unattended(), pickers.h) no picker opens. The checks drive it through REKIT_SETUP_*:
//   REKIT_SETUP_FRAMES=n        end (cancelled) after n frames
//   REKIT_SETUP_PICKER=cancel   the picker answered as cancelled; =path:<file> answered with that file (no picker opens)
//   REKIT_SETUP_GO=1            INSTALL pressed once a find is offered
//   REKIT_SETUP_PICK_OTHER=1    the find's plate pressed once (other files picked)
//   REKIT_SETUP_FILES_TO=<dir>  CHANGE FOLDER's pick
//   REKIT_SETUP_FIND=1          the setup window at once, its search, and the file picker when the search finds none
//   REKIT_SETUP_NAV=next,prev,press,back   keys, in order, at the start
//   REKIT_SETUP_NAV_INSTALL=... the same keys when the first install or download starts (CANCEL, the link)
//   REKIT_SETUP_SHOT=<file.bmp> the frame saved at REKIT_SETUP_FRAMES' end (else the first), then the end (cancelled)
//   REKIT_SETUP_DOWNLOAD=1      DOWNLOAD pressed once it is offered (a fresh setup with none found starts it anyway)
//   REKIT_SETUP_ALLOW_FILE=1    file:/// addresses taken for the catalog and its sources (checks only)
//   REKIT_SETUP_PREVIEW=download|copy|asking|files   a review's picture: the small window downloading, copying a
//                               found copy or asking the catalog, or the setup window with a find and a download
//                               offered, held with made-up numbers until REKIT_SETUP_FRAMES (nothing read or fetched)
//   RE_KIT_TEST=1               a game's test run: no network at all (only file:/// with REKIT_SETUP_ALLOW_FILE)
#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "rekit/sdl/theme.h"
#include "rekit/search.h"
#include "rekit/sources.h"
#include "rekit/status.h"

namespace rekit::sdl {

struct Progress {
  std::string stage;    // "Copying the game files"
  double fraction = 0;  // of the whole operation, 0..1
  std::string message;  // the current file, for a second line
};
using ProgressFn = std::function<bool(const Progress&)>;  // false cancels

struct InstallResult {
  bool ok = false, cancelled = false;
  std::string summary;      // one or two sentences for the player
  std::string install_dir;  // the folder now holding the game's files
};

struct SetupOptions;
// A copy of the game on this computer, for a launcher's list of them (Game::copies).
struct CopyRow {
  std::string path;                  // its folder
  std::string data;                  // its data's version ("1.2"), "" unknown
  bool in_use = false;               // the copy the game plays now
  std::vector<std::string> missing;  // what the game would lack with it, a few plain words each ("music")
};

// The recipe as the screen needs it (the rest of a recipe stays in the game).
struct Game {
  std::string id;           // the launcher contract's ("mech1"), for the log
  std::string title;        // "MechWarrior 1": the sentences' name
  std::string folder_key;   // the game folder's mark (game_folder.h): "mw1r"
  std::string folder_name;  // a game folder made inside a picked one: "MechWarrior 1 Rebuild"
  std::string program;      // the file a player picks in an installed copy: "BTECH.EXE"
  SourceRules rules;        // what find_source looks for
  // The automatic search (the first start, SEARCH AGAIN): a copy found on this computer, or "". Null: the
  // kit's install_candidates(hints, program_dir), each looked at with find_source.
  std::function<std::string()> search;
  SearchHints hints;
  // A copy find_source can't read (a disk image of another machine): called when find_source found no copy of the game
  // in a pick, before `recognise`; true with `found` set (kind, game == id, and what `install` needs; an empty path
  // becomes the pick's) offers it like any find. With it set, a picked file that is none of the kit's kinds (a program,
  // a disc image, an archive) is asked about itself first, then its folder is looked at as without it; without it such
  // a file is only ever its folder. Null: none. Runs on the worker thread; given any path (unreadable, gone), and the
  // kit catches what it throws (no find).
  std::function<bool(const std::string& path, Found* found)> find;
  // The kit's words for what to pick, in a game's own; each "": the kit's.
  std::string pick_words;  // "a download, or DEMO.EXE" (after "Pick it: " and "Pick the copy to use: ")
  std::string find_sub;    // FIND THE GAME's second line, in a launcher's served setup (the kit's window shows none)
  std::string find_show;   // "a download (.zip, .7z) or the folder where it is installed" (after "Pick ")
  // Words for a find ("An installed copy of MechWarrior 1, revision 1990-01-22."); null: the kit's.
  std::function<std::string(const Found&)> describe;
  // What a pick holds when find_source found no copy in it (another version of the game, a disk of a set): one
  // sentence, shown as plain text in place of the kit's "No copy of <title> was found in <pick>."; "" keeps the kit's.
  // Null: the kit's. Runs on the worker thread; given any path (unreadable, gone), and the kit catches what it throws.
  std::function<std::string(const std::string& path)> recognise;
  // Installs a find into `out_dir` (staged and swapped in: a failure or a cancel leaves nothing), its media (music,
  // converted movies) into `media_dir`. Runs on a worker thread.
  std::function<InstallResult(const Found&, const std::string& out_dir, const std::string& media_dir, const ProgressFn&)>
      install;
  // Where music and converted movies go for an install; null: media_dir() (game_folder.h).
  std::function<std::string(const std::string& install, const std::string& user_folder)> media;
  // A launcher's extras, served only (setup_serve.h; 2026-11-25). Null: the game does not answer that ask. Each runs on
  // the serving thread; a throw is a failure in its words.
  // --copies: every copy of the game this computer holds, the one in use (`options.in_use`) among them.
  std::function<std::vector<CopyRow>(const SetupOptions& options)> copies;
  // --add <pick>: what the pick holds that the files in use lack (a disc's music, say), added to them.
  std::function<InstallResult(const std::string& pick, const SetupOptions& options, const ProgressFn& progress)> add;
  // --first-run <step>: one of the status's pending steps ("movies"), done now.
  std::function<InstallResult(const std::string& step, const SetupOptions& options, const ProgressFn& progress)> first_run;
  // The game's original files from a catalog (DESIGN.md, Original files; 2026-11-26): `original_catalogs` the https://
  // addresses of one catalog (JSON shaped as mw3-re's catalog/games.json), asked in order; `catalog_keys` the public
  // keys (64 hex digits) its signature (<address>.sig, DESIGN.md, Signed data) must check against: the first that does
  // is used, an unsigned or wrongly signed one passed over; `original_key` the game's entry in it. When that entry
  // lists "original" (or "compact") sources, a fresh setup (no files in use) with no copy found on this computer
  // downloads at once in the small window ("GETTING THE ORIGINAL GAME FILES", from <note>, <size>); the setup window
  // offers it again under DOWNLOAD THEM. The first source that comes whole and matches its SHA-256 is installed at
  // once, and deleted after. No address, no key or no entry key: none asked for. A test run (RE_KIT_TEST=1) asks none.
  std::vector<std::string> original_catalogs, catalog_keys;
  std::string original_key;
  // This program's version or release tag ("v1.7.0", "pm-v1.3.0"), for a catalog's "compact" sources that only a game
  // from some version on reads (read_original's game_min); "": those are passed over.
  std::string version;
};

struct SetupOptions {
  std::string user_folder;  // the game's user folder (saves, settings)
  std::string default_out;  // where INSTALL puts the files unless CHANGE FOLDER picks another (<user folder>/install)
  std::string in_use;       // the files in use, when choosing again ("In use: ...")
  std::string notice;       // what is wrong with them, a sentence in the problem colour on the find screen ("": nothing)
  // Said on the find screen when a pick is the copy already in use ("Those are the files in use. Pick another copy."):
  // the player picks on. "" (or no in_use): such a pick ends the setup, ready, when it is the game's own copy.
  std::string same_words;
  std::string look_first;   // a find to offer at once ("USE THESE GAME FILES?")
  std::string temp_dir;     // where DOWNLOAD THE GAME FILES puts its folder; "": the system's temp folder
  std::string program_dir;  // this program's folder, for the kit's search
  bool play_button = false; // PLAY in QUIT's place (choosing again at a start): goes on with in_use
  bool hidden = false;      // the window never shown (checks); with no video driver asked for, SDL's dummy one
  bool fullscreen = false;  // not used (2026-11-27): the setup is always an ordinary window, so the pickers behave
  int width = 960, height = 540;  // the setup window's (the small window is the kit's own size)
  std::string log_prefix = "setup: ";
  // Each log line (without the prefix); null: printed on stdout after the prefix.
  std::function<void(const std::string&)> log;
};

struct SetupResult {
  SetupExit exit = kSetupCancelled;
  std::string install_dir;  // kSetupReady: the game's files
  std::string error;        // kSetupFailed: why
};

SetupResult run_setup(const Game& game, const Theme& theme, const SetupOptions& options);

}  // namespace rekit::sdl
