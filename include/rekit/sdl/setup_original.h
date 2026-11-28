#pragma once
// A game's original files fetched from a catalog's sources (rekit/original.h; DESIGN.md, Original files) with the
// system's curl: HTTPS only (file:/// only when a check allows it), each file's size and SHA-256 checked, into a
// folder of the kit's own in the system's temp folder, marked as the kit's. The setup screen's DOWNLOAD THE GAME FILES.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "rekit/original.h"
#include "rekit/sdl/setup_screen.h"

namespace rekit::sdl {

constexpr const char* kOriginalMark = "rekit-original-download.txt";

// The system's curl (Windows' own in System32, never one on PATH; /usr/bin and the usual places elsewhere); "": none.
std::string curl_program();
// RE_KIT_TEST=1 (docs/plan-in-game-menu.md, Test mode): a game's test run, where the kit makes no network request at
// all. fetch_url refuses every address but file:///, and the setup screen asks no catalog for one.
bool kit_test_mode();

// `url` into `to` (written as `to`.part, renamed when whole), `progress(fraction of size)` asked as it goes (false
// cancels: the part removed, `cancelled` set). `seconds` > 0 caps the whole fetch; else it ends only when the server
// sends under 1 KB/s for a minute. `fresh`: "Cache-Control: no-cache" and "Pragma: no-cache" sent, so a proxy
// between serves no stale copy (a catalog and its .sig; a site may mark its static files good for years).
// REKIT_CURL_ARGS_TO (checks): curl's arguments appended there, a line each.
bool fetch_url(const std::string& url, const std::string& to, uint64_t size, int seconds, bool allow_file,
               const std::function<bool(double)>& progress, std::string* error, bool* cancelled, bool fresh = false);

// <temp>/rekit-original-<id>, made and marked; "" with `error` when that name holds a folder of someone else's
// (no mark, not empty), which is never written to. `temp` "": the system's temp folder.
std::string original_dir(const std::string& temp, const std::string& game_id, std::string* error);
// The folder's mark and the folder itself, once nothing else is in it.
void tidy_original_dir(const std::string& dir);

// The catalog's text from the first of `catalogs` (each fetched with <address>.sig into `dir`, both removed after) signed
// by one of `keys` (rekit/sign.h). False with `why`: each address's reason, "; " between; `cancelled` when go_on stopped.
bool fetch_signed_catalog(const std::vector<std::string>& catalogs, const std::vector<std::string>& keys, const std::string& dir,
                          bool allow_file, const std::function<bool(double)>& go_on, std::string* text, std::string* why,
                          bool* cancelled = nullptr);

// The catalog fetched from each of `catalogs` in turn with its signature (<address>.sig), into `dir` (removed after),
// until one is signed by one of `keys` (rekit/sign.h); that one read for `key`'s sources. False with `why`: none
// was (each address's reason, "; " between), or the signed one lists none. `game_version`: read_original's.
bool catalog_sources(const std::vector<std::string>& catalogs, const std::vector<std::string>& keys, const std::string& key,
                     const std::string& dir, bool allow_file, const std::function<bool(double)>& go_on,
                     std::vector<OriginalSource>* out, std::string* why, const std::string& game_version = "");

// Each source in turn into `dir`/its name, checked; the first whole and matching one's path. A failed one is removed
// and the next tried. ok false with the reasons ("; " between them) in summary, or cancelled.
InstallResult fetch_original(const std::vector<OriginalSource>& sources, const std::string& dir, bool allow_file,
                             const ProgressFn& progress);

}  // namespace rekit::sdl
