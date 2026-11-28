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
  bool compact = false;                 // from "compact" (a smaller repack only newer games read), not "original"
};
constexpr int kMaxOriginalSources = 4;

// The sources of the entry whose "key" is `key` in `catalog_json`'s "games", each checked: an https:// address with a
// plain host (file:/// as well when `allow_file`, for checks), a 64-hex sha256, a size in bytes, a plain file name
// (letters, digits, spaces and "-_.()',"). Its "original" sources first, in order, then its "compact" ones (2026-11-27:
// a smaller repack of the same files that only newer games read; at most kMaxOriginalSources each). A compact source
// may give "game_min", a dotted version ("1.7.0"): it is left out unless `game_version` (a version or a release
// tag, version_number) is known and not lower (compared number by number). False with `why`: not a catalog, no such entry, neither list gives a source this
// game can use, or a source is bad (that whole list is then not used).
bool read_original(const std::string& catalog_json, const std::string& key, bool allow_file, std::vector<OriginalSource>* out,
                   std::string* why, const std::string& game_version = "");

// The page where the game's problems are reported (the in-game menu's REPORT A PROBLEM; 2026-11-27): the "report" of the
// entry whose "key" is `key` in `catalog_json`'s "games", an https:// address with a plain host. "" when there is no such
// entry or field, or it is not such an address (the game's built-in page is used then).
std::string catalog_report(const std::string& catalog_json, const std::string& key);

// `a` against `b` as dotted versions ("1.10.2" > "1.9"), number by number, missing parts as 0: -1, 0 or 1. A part that
// is not all digits makes the whole compare -2 (not comparable).
int compare_versions(const std::string& a, const std::string& b);
// A release tag's version: the leading digits and dots after its last "v" ("pm-v1.2.12" and "v1.6.1-4-gabc" give
// "1.2.12" and "1.6.1"); a plain "1.6.1" as it is; "" when there are none.
std::string version_number(const std::string& tag);

}  // namespace rekit
