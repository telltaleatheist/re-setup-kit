#pragma once
// A game's original files from a catalog (2026-11-26, DESIGN.md, Original files): JSON in the shape of mw3-re's
// catalog/games.json, whose entry for the game may list "original" sources, the same file at each address, tried in
// order. The kit names no catalog and no game: a game's config gives both (sdl::Game::original). No catalog lists a
// source today; without one, nothing is ever fetched.
#include <cstdint>
#include <string>
#include <vector>

namespace rekit {

struct OriginalSource {
  std::string url, sha256, name, note;  // sha256 lower case; name the file's ("Demo Game (USA).zip")
  uint64_t size = 0;                    // in bytes
};
constexpr int kMaxOriginalSources = 4;

// The sources of the entry whose "key" is `key` in `catalog_json`'s "games", each checked: an https:// address with a
// plain host (file:/// as well when `allow_file`, for checks), a 64-hex sha256, a size in bytes, a plain file name
// (letters, digits, spaces and "-_.()',"). False with `why`: not a catalog, no such entry, it lists no sources, or one
// of them is bad (the whole list is then not used).
bool read_original(const std::string& catalog_json, const std::string& key, bool allow_file, std::vector<OriginalSource>* out,
                   std::string* why);

}  // namespace rekit
