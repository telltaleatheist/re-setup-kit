// The kit's settings (rekit/settings.h): the shipped file's defaults, the clamps, and the layers (shipped, the signed
// catalog's live values, the player's own).
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/settings.h"

namespace fs = std::filesystem;

namespace {
std::string dir_in_tmp(const char* name) {
  const fs::path d = fs::path(REKIT_TEST_TMP) / name;
  std::error_code ec;
  fs::remove_all(d, ec), fs::create_directories(d, ec);
  return d.string();
}
void write(const std::string& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }
}  // namespace

// The shipped file holds every value at the kit's default, so a game that ships it unchanged behaves as before.
TEST(kit_settings_shipped_file_is_the_defaults) {
  std::ifstream f(std::string(REKIT_SOURCE_DIR) + "/assets/setup-kit-settings.json", std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  std::vector<std::string> notes;
  const rekit::KitSettings d, s = rekit::read_kit_settings(text, rekit::KitSettings{}, [&](const std::string& l) { notes.push_back(l); });
  rekit::KitSettings off;
  off.hint_starts = 9, off.hint_seconds = 30, off.hint_fade_in_seconds = 1, off.hint_fade_out_seconds = 2, off.check_listed = 20;
  off.click_volume = off.hover_volume = 0.1, off.update_check_hours = 100, off.catalog_seconds = off.releases_seconds = 50;
  off.releases_count = 30, off.sums_seconds = 99, off.count_seconds = 30, off.download_connect_seconds = 50;
  off.download_slow_bytes_per_second = 99, off.download_slow_seconds = 99, off.live_every_hours = 99;
  const rekit::KitSettings o = rekit::read_kit_settings(text, off, nullptr);  // every key is in the file
  std::printf("  %zu notes; hint %d starts %.1f s\n", notes.size(), s.hint_starts, s.hint_seconds);
  EXPECT(!text.empty() && notes.empty());
  EXPECT(o.hint_starts == d.hint_starts && o.hint_seconds == d.hint_seconds && o.hint_fade_in_seconds == d.hint_fade_in_seconds &&
         o.hint_fade_out_seconds == d.hint_fade_out_seconds && o.check_listed == d.check_listed && o.click_volume == d.click_volume &&
         o.hover_volume == d.hover_volume && o.update_check_hours == d.update_check_hours && o.catalog_seconds == d.catalog_seconds &&
         o.releases_seconds == d.releases_seconds && o.releases_count == d.releases_count && o.sums_seconds == d.sums_seconds &&
         o.count_seconds == d.count_seconds && o.download_connect_seconds == d.download_connect_seconds &&
         o.download_slow_bytes_per_second == d.download_slow_bytes_per_second &&
         o.download_slow_seconds == d.download_slow_seconds && o.live_every_hours == d.live_every_hours &&
         o.report_url.empty() && o.catalogs.empty());
}

// Out of range: the nearest end; a wrong type or a non-https address: the value before, said; unknown keys ignored.
TEST(kit_settings_clamped_and_kept) {
  std::vector<std::string> notes;
  rekit::KitSettings base;
  base.report_url = "https://example.com/before";
  const rekit::KitSettings s = rekit::read_kit_settings(
      R"({"hint_seconds": 999, "update_check_hours": 1, "hint_starts": "three", "report_url": "http://example.com/x",
          "catalogs": ["https://a.example/c.json", "ftp://b"], "click_volume": 0.5, "unknown": 1, "check_listed": 7.6})",
      base, [&](const std::string& l) { notes.push_back(l); });
  for (const std::string& n : notes) std::printf("  %s\n", n.c_str());
  EXPECT(s.hint_seconds == 60 && s.update_check_hours == 12 && s.hint_starts == 3 && s.report_url == "https://example.com/before");
  EXPECT(s.catalogs.empty() && s.click_volume == 0.5 && s.check_listed == 8 && notes.size() == 5);
  notes.clear();
  const rekit::KitSettings t = rekit::read_kit_settings("not json", base, [&](const std::string& l) { notes.push_back(l); });
  EXPECT(t.report_url == base.report_url && notes.size() == 1);
}

// The layers: the shipped file, then the signed catalog's values (all games', the game's own, its report page), then
// the player's file; the live layer kept in the data folder and asked again only when live_every_hours is up.
TEST(kit_settings_layers_shipped_live_player) {
  const std::string prog = dir_in_tmp("settings-prog"), user = dir_in_tmp("settings-user");
  write(prog + "/setup-kit-settings.json", R"({"hint_seconds": 5, "report_url": "https://example.com/shipped", "check_listed": 4})");
  std::vector<std::string> log;
  rekit::reset_kit_settings();
  EXPECT(rekit::live_kit_settings_due(user, std::time(nullptr)));
  rekit::load_kit_settings(prog, user, [&](const std::string& l) { log.push_back(l); });
  const rekit::KitSettings a = rekit::kit_settings();
  const std::string catalog =
      R"({"setup_kit": {"hint_seconds": 6}, "games": [{"key": "other", "report": "https://example.com/other"},
         {"key": "demo", "report": "https://example.com/live", "setup_kit": {"check_listed": 9}}]})";
  const bool applied = rekit::apply_live_kit_settings(catalog, "demo", user, [&](const std::string& l) { log.push_back(l); });
  const rekit::KitSettings b = rekit::kit_settings();
  const auto now = static_cast<int64_t>(std::time(nullptr));
  const bool due_now = rekit::live_kit_settings_due(user, now), due_later = rekit::live_kit_settings_due(user, now + 5 * 3600);
  write(user + "/setup-kit-settings.json", R"({"hint_seconds": 7})");
  rekit::reset_kit_settings();
  rekit::load_kit_settings(prog, user, nullptr);
  const rekit::KitSettings c = rekit::kit_settings();
  rekit::load_kit_settings(prog, "", nullptr);  // once a process: a second load changes nothing
  const rekit::KitSettings c2 = rekit::kit_settings();
  rekit::reset_kit_settings();
  std::printf("  shipped: %.0f s, %s, %d; live: %.0f s, %s, %d; player: %.0f s, %s; due %d then %d\n", a.hint_seconds,
              a.report_url.c_str(), a.check_listed, b.hint_seconds, b.report_url.c_str(), b.check_listed, c.hint_seconds,
              c.report_url.c_str(), due_now, due_later);
  EXPECT(a.hint_seconds == 5 && a.report_url == "https://example.com/shipped" && a.check_listed == 4);
  EXPECT(applied && b.hint_seconds == 6 && b.report_url == "https://example.com/live" && b.check_listed == 9);
  EXPECT(!due_now && due_later && c.hint_seconds == 7 && c.report_url == "https://example.com/live" && c2.hint_seconds == 7);
}
