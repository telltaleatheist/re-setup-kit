#pragma once
// The system's file and folder pickers (from mw3-re's system_pickers.h), the one way a game's setup opens them. In a
// run no player is at (a hidden or scripted run, a test: the game says so through unattended()) no picker ever opens:
// the ask is answered at once as cancelled and SDL is never called. A hidden test once put a picker on the player's
// screen.
#include <SDL3/SDL_dialog.h>

#include <functional>

namespace rekit::sdl {

// Whether no player is at this run; the game sets it (mw3-re: platform::pointer_hands_off). Default: a player is.
inline std::function<bool()>& unattended() {
  static std::function<bool()> f = [] { return false; };
  return f;
}

enum class PickerKind { kOpenFile, kOpenFolder, kSaveFile };

struct PickerAsk {
  PickerKind kind = PickerKind::kOpenFile;
  SDL_DialogFileCallback done = nullptr;
  void* user = nullptr;
  SDL_Window* parent = nullptr;
  const SDL_DialogFileFilter* filters = nullptr;
  int filter_count = 0;
  const char* start = nullptr;  // the folder (or file name) it opens at; null: the system's choice
};

// What opens a picker: SDL's, unless a test puts a stand-in here (so a test of the guard can never open one).
inline std::function<void(const PickerAsk&)>& picker_opener() {
  static std::function<void(const PickerAsk&)> open = [](const PickerAsk& a) {
    switch (a.kind) {
      case PickerKind::kOpenFile:
        SDL_ShowOpenFileDialog(a.done, a.user, a.parent, a.filters, a.filter_count, a.start, false);
        break;
      case PickerKind::kOpenFolder: SDL_ShowOpenFolderDialog(a.done, a.user, a.parent, a.start, false); break;
      case PickerKind::kSaveFile: SDL_ShowSaveFileDialog(a.done, a.user, a.parent, a.filters, a.filter_count, a.start); break;
    }
  };
  return open;
}

// Opens the picker; false (and `done` called at once with no file, as SDL says a cancel) in an unattended run.
inline bool open_picker(const PickerAsk& a) {
  if (unattended()()) {
    static const char* const kNone[] = {nullptr};
    if (a.done) a.done(a.user, kNone, -1);
    return false;
  }
  picker_opener()(a);
  return true;
}

}  // namespace rekit::sdl
