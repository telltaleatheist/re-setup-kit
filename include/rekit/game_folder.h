#pragma once
#include <string>

// Where a game goes (from mw3-re's sdl_games and sdl_install, its "Where a game goes"): each game has a folder of its
// own that the player may pick, holding its program and its game files (<folder>/install); saves and settings stay
// in the game's user folder. A folder is a game's when it carries the game's mark, rebuild-game-folder.txt with a
// "game=<key>" line (the key the launcher names the game by: "mw3", "pm", "mw1", "mw1r"); a folder holding it is
// made a games folder first (rebuild-games.txt). The files and their lines are the same as mw3-re writes, so a
// folder either made is read by both.
namespace rekit {

inline constexpr const char* kGameFolderMark = "rebuild-game-folder.txt";
inline constexpr const char* kGamesFolderMark = "rebuild-games.txt";

// The folder the game goes in for a pick: the pick itself when it is missing, empty or already this game's (its
// mark's game=<key>), else a folder named `folder_name` inside it ("MechWarrior 3"): a drive's top or someone's
// folder is never filled loose.
std::string game_folder_in(const std::string& picked, const std::string& key, const std::string& folder_name);

// `dir` made this game's before anything goes in: true when it already carries this game's mark; else, when it is
// missing or empty, made, given the games folder's mark and the game's. A folder holding anything else, or another
// game's, is refused (false, `error` in plain words) and nothing is written.
bool claim_game_folder(const std::string& dir, const std::string& key, std::string* error);

// Where a game's music and converted movies go: the folder above its install when that folder carries a game's
// mark (a picked game folder), else the user folder.
std::string media_dir(const std::string& install, const std::string& user_folder);

}  // namespace rekit
