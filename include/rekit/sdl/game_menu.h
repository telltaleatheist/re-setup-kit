#pragma once
// The setup menu inside a running game (docs/plan-in-game-menu.md sections 1 and 3, step 2b): a layer the game draws
// over its own frame, opened by Shift+Alt+F1 (Cmd+Shift+F1 on the Mac) at any moment, closed by Esc back to the game.
// The game keeps running underneath with its clock paused. The kit names no game: the game fills in MenuHooks (its
// drawing, its pause, its paths, its install type, its own entries), and the look is its Theme (theme.h).
//
// In the game's loop:
//   GameMenu menu(hooks, theme);
//   while (SDL_PollEvent(&e)) menu.event(e);   // every event; the kit hands the game's on to hooks.deliver
//   ...the game's update (paused while the menu is open) and its frame...
//   menu.frame();                              // after the frame, before it is shown: hooks.draw when there is a picture
//   present
//
// F1 of the keys never reaches the game, down or up. Their Shift and Alt (Cmd) reach it at once, as every key does (a
// game may bind them alone), and go up for it as the menu opens: everything the game holds down then (keys, mouse and
// pad buttons) gets its up, and the real ups that follow are the kit's. What goes down in the menu stays the kit's. RE_KIT_TEST=1 turns the keys and the hint off (the screenshot goldens and recorded runs stay as they are);
// RE_LAUNCHER (set by the launcher) turns the hint off. A game's own button opens it with show().
#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rekit/files.h"
#include "rekit/sdl/theme.h"
#include "rekit/sdl/update.h"

namespace rekit::sdl {

// The menu's picture: RGBA bytes (SDL_PIXELFORMAT_RGBA32, straight alpha) the size of the window in pixels, to be
// drawn over the whole window one to one, blended (source alpha). `serial` changes only when the picture does, so a
// game uploads it again only then.
struct MenuImage {
  const void* pixels = nullptr;
  int w = 0, h = 0, pitch = 0;
  uint64_t serial = 0;
};

// One of the game's own entries (Revenge's UPGRADES): shown after the kit's sections, before RETURN TO GAME. Picking it
// closes the menu, resumes the game as Esc does, and then calls `pick`: the callback runs after the game has resumed,
// so a game whose page must open over a paused battle pauses again itself.
struct MenuEntry {
  std::string label;          // in the game's words, shown in capitals
  std::function<void()> pick;
};

struct MenuHooks {
  // --- Drawing and input (required).
  SDL_Window* window = nullptr;                      // its size and pixel density set the picture's
  std::function<void(const MenuImage&)> draw;        // the picture over the frame, each frame there is one
  std::function<void(const SDL_Event&)> deliver;     // an event that is the game's, in order
  // --- The game's clock: frozen while the menu is open (fixed-step ticks, timed narration, per-frame polls), so
  // closing it causes no catch-up burst. Null: nothing paused.
  std::function<void()> pause, resume;
  // --- What the pages show.
  std::string title;        // "MechWarrior 3": the sentences' name (the header is theme.header, else this in capitals)
  std::string version;      // this program's ("1.7.0"), said on REPORT A PROBLEM
  std::string user_folder;  // saves, settings, logs, music, mods (the game's own overrides honored); the hint's count
  std::string log_file;     // the log a report attaches
  std::string files_in_use; // GAME FILES: the copy in use ("" none known)
  std::string report_url;   // the page where problems are reported, the game's built-in one (its repo's issues page);
                            // a catalog's "report" replaces it (set_report_url, catalog_report in original.h)
  InstallKind install = InstallKind::kUnknown;  // how this program was installed (rekit/update.h), for UPDATES
  // CHECK MY FILES: the program's folder (its package's rebuild-files.sha256 at the top; "" none) and the files the
  // recipe leaves out on purpose; the game files (files_in_use) are checked against the record the kit made at install.
  std::string program_dir;
  std::vector<LeaveOut> leave_out;
  // Files the game rewrites in its own folder (an original's config file, a high-score table), matched as leave_out
  // is: in the record, never compared.
  std::vector<LeaveOut> rewritten;
  // The game's own check of game files that have no record (an install from before records were kept): the problems
  // in the game's words, none when they are right. When it finds none, the kit writes their record, so later checks
  // use it. Runs on the check's thread (it only reads files). Null: "no record" is said, as for the program.
  std::function<std::vector<std::string>()> check_game_files;
  // RESET SETTINGS: the game puts its settings back to its defaults, in memory and on disk (it is running: only it
  // can), true when done. `settings` (its settings files or folders) are copied to Backups first. Null: no reset here.
  std::function<bool()> reset_settings;
  std::vector<std::string> settings;
  // GAME FILES' CHANGE (RESTARTS THE GAME), after the player agrees: the game closes and opens its setup in its own
  // window (relaunch_for_setup does the second half). Null: no CHANGE.
  std::function<void()> restart_for_setup;
  // UPDATES (step 3): where this game's releases are and what an update replaces (sdl/update.h); recipe.repo ""
  // shows no check. With one, a start not from the launcher checks once a day and asks (UPDATE NOW, NOT NOW, DON'T ASK
  // AGAIN). `quit`: the game's clean exit after UPDATE NOW has put the update in place or left its helper waiting;
  // null pushes SDL_EVENT_QUIT.
  UpdateSource update;
  std::function<void()> quit;
  std::vector<MenuEntry> entries;
  // Each log line ("menu: open"); null: printed on stdout after "rekit: ".
  std::function<void(const std::string&)> log;
  // --- For checks; each null: the real thing.
  std::function<bool(const std::string& url)> open_url;   // SDL_OpenURL (a folder as a file:// address)
  std::function<bool(const std::string& text)> copy_text; // SDL_SetClipboardText
  std::function<uint64_t()> now_ms;                       // SDL_GetTicks
};

class GameMenu {
 public:
  // One a start: counts the start for the hint (in user_folder) when the hint may show.
  GameMenu(MenuHooks hooks, const Theme& theme);
  ~GameMenu();
  GameMenu(const GameMenu&) = delete;
  GameMenu& operator=(const GameMenu&) = delete;

  // Every SDL event, first. The game's are handed to hooks.deliver at once (with the ups the menu's opening makes);
  // true when the kit kept this one.
  bool event(const SDL_Event& e);
  // After the game's frame, before it is shown: hooks.draw with the menu or the hint, nothing when neither shows.
  void frame();

  bool is_open() const;
  void show();   // the game's own button: opens on GAME FILES
  void hide();   // as Esc
  void set_report_url(const std::string& url);  // a catalog's "report" (https only; anything else is ignored)

  // The keys' modifier on this system: SDL_KMOD_GUI | SDL_KMOD_SHIFT on the Mac, SDL_KMOD_ALT | SDL_KMOD_SHIFT elsewhere.
  static SDL_Keymod keys();
  // The hint's words on this system ("Shift + Alt + F1").
  static std::string keys_words();

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// The second half of CHANGE: this program (self_path(), program_record.h: its AppImage when it runs from one) started
// again with `args` (its setup flag, "--setup") in its own process, not waiting for it. The game calls it while
// closing. False when it could not be started, or this program's path is unknown (a Mac app run translocated).
bool relaunch_for_setup(const std::vector<std::string>& args);

}  // namespace rekit::sdl
