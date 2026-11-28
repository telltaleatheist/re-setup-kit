#pragma once
// The kit's tunable values, read from a data file instead of written into the code (2026-11-28): the setup menu's hint,
// its sounds, the update check's interval and the network time limits. Defaults equal the kit's behavior before the
// file, so a missing file changes nothing. Each value is clamped to its range; a wrong type or a bad address falls
// back. Trust settings (signing keys, https-only, checksum and signature checks) are never in it. DESIGN.md, Settings.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rekit {

struct KitSettings {
  int hint_starts = 3;                  // 0 to 20: the first starts that show the setup hint (0: never)
  double hint_seconds = 10;             // 3 to 60
  double hint_fade_in_seconds = 0.4;    // 0 to 2
  double hint_fade_out_seconds = 0.8;   // 0 to 3
  double update_notice_seconds = 15;     // 3 to 60: the corner's UPDATE AVAILABLE notice
  int check_listed = 6;                 // 1 to 50: damaged files CHECK MY FILES names before "and N more"
  double click_volume = 1.0;            // 0 to 1
  double hover_volume = 0.7;            // 0 to 1
  std::string report_url;               // https only; empty: the game's own (MenuHooks::report_url). The signed
                                        // catalog's "report" for the game, when one was read, comes before it.
  std::vector<std::string> catalogs;    // https only, up to 5: where the signed catalog is asked, in order; empty:
                                        // the game's own (UpdateSource::catalogs). The keys stay in the game.
  int update_check_hours = 24;          // 12 to 720: the start-up check at most this often
  int catalog_seconds = 20;             // 5 to 120: time allowed for the catalog, and again for its signature
  int releases_seconds = 20;            // 5 to 120: time allowed for a game's releases list
  int releases_count = 20;              // 10 to 40: releases asked for in that list
  int sums_seconds = 20;                // 10 to 300: time allowed for SHA256SUMS, and again for its signature
  int count_seconds = 10;               // 3 to 60: time allowed for the update count
  int download_connect_seconds = 20;    // 5 to 120
  int download_slow_bytes_per_second = 1024;  // 1 to 65536: a download slower than this...
  int download_slow_seconds = 60;       // 15 to 600: ...for this long is given up
  int live_every_hours = 4;             // 1 to 168: the signed catalog asked again for these values at most this often
};

constexpr const char* kKitSettingsFile = "setup-kit-settings.json";

// The values in use (a copy: a live catalog may change them while the game runs): the defaults until
// load_kit_settings, then the layers it read.
KitSettings kit_settings();

// `text` (a setup-kit-settings.json) over `base`: each known key clamped into its range, a wrong type or address
// kept at base. Every change is said to `note` ("hint_seconds 99 is above 60: 60 used").
KitSettings read_kit_settings(const std::string& text, const KitSettings& base,
                              const std::function<void(const std::string&)>& note);

// The layers, each optional, each over the one before (DESIGN.md, Settings):
//   1. the kit's defaults;
//   2. the shipped file, <program_dir>/setup-kit-settings.json (each game's own, beside its program);
//   3. the live values: the signed catalog's "setup_kit" for every game, then the game's own entry's "setup_kit" and
//      its "report", as last read (<user_folder>/setup-kit-live.json, written by apply_live_kit_settings);
//   4. the player's own file, <user_folder>/setup-kit-settings.json.
// Said to `log`. Once a process: later calls do nothing (the first with a program folder reads).
void load_kit_settings(const std::string& program_dir, const std::string& user_folder,
                       const std::function<void(const std::string&)>& log);

// A signed catalog just read (its signature already checked): its values for game `key` kept as the live layer in
// <user_folder>/setup-kit-live.json and the values in use read again. False when it holds nothing for the game.
bool apply_live_kit_settings(const std::string& catalog_json, const std::string& key, const std::string& user_folder,
                             const std::function<void(const std::string&)>& log);

// Whether the live layer is older than live_every_hours (or missing), so the catalog may be asked again.
bool live_kit_settings_due(const std::string& user_folder, int64_t now);

// Tests: the values put back to `s` and the next load allowed again.
void reset_kit_settings(const KitSettings& s = {});

}  // namespace rekit
