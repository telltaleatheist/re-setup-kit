#pragma once
// The setup without a screen, for the launcher: a game started from it is set up on the launcher's own screens, the
// recipe still the game's (DESIGN.md, The launcher contract). After `--setup`:
//
//   --describe                     the find screen's words (title, pick_words, find_sub, find_words), into
//   --search                       a copy found on this computer (path, "" none)
//   --look <pick>                  what a pick holds: ok, words, into; or the summary why not; in_place: the game's own
//                                  copy, set up already
//   --install <pick> [--folder <dir>]   the pick looked at again and copied in: into <dir>/install, the folder claimed
//                                  for the game first, else the default; ok, where, or the summary why not
//
// Each prints SetupMessage lines (rekit/status.h) on stdout, the last one `done`, and exits kSetupReady, kSetupCancelled
// (a "cancel" line on stdin stops a look or an install, which then leaves nothing behind) or kSetupFailed. No window,
// no picker. A game that answers these says so in its --status (Status::setup = kServedSetup).
#include <string>
#include <vector>

#include "rekit/sdl/setup_screen.h"

namespace rekit::sdl {

// `args`: what follows `--setup`. True when they ask for a served setup (an empty list, or anything else, is the
// screen's).
bool serves_setup(const std::vector<std::string>& args);

// Answers them. `result`: exit and, after an install or a copy already in place, install_dir: the game saves it as the
// files in use, as after run_setup, before it exits.
SetupResult serve_setup(const Game& game, const SetupOptions& options, const std::vector<std::string>& args);

}  // namespace rekit::sdl
