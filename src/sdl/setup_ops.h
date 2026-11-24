#pragma once
// What the setup screen and the served setup (setup_serve.h) do alike: the search, a look at a pick, the words.
#include <string>

#include "rekit/sdl/setup_screen.h"

namespace rekit::sdl::detail {

std::string what_to_pick(const Game& g);
std::string find_sub(const Game& g);    // FIND THE GAME's second line
std::string find_words(const Game& g);  // the find screen's sentences
std::string found_words(const Game& g, const Found& f);

// The automatic search: a copy found on this computer, or "".
std::string search_copy(const Game& g, const std::string& program_dir);

// What a pick is looked at as: an image or an archive as it is; a folder as it is; any other file for its folder
// (`file` the file itself when the game's find hook may know it).
void pick_source(const Game& g, const std::string& path, std::string* source, std::string* file);

// The look itself: `found` set; ok when it is a copy of the game, else the summary says why not.
InstallResult look(const Game& g, const std::string& path, const std::string& file, Found* found, const ProgressFn& progress);

// The game's files for an install: `folder`/install for a picked game folder, else `default_out`.
std::string out_dir(const std::string& folder, const std::string& default_out);

// Installs `found` (the folder claimed for the game first when one was picked).
InstallResult install(const Game& g, const Found& found, const std::string& folder, const SetupOptions& o, const ProgressFn& progress);

}  // namespace rekit::sdl::detail
