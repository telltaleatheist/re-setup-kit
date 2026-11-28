#include "setup_ops.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "rekit/archive.h"
#include "rekit/game_folder.h"
#include "rekit/manifest.h"

namespace fs = std::filesystem;

namespace rekit::sdl::detail {
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool has_disc_clue(const SourceRules& r) {
  return std::any_of(r.clues.begin(), r.clues.end(), [](const Clue& c) { return c.kind == Clue::kDisc; });
}

// What the game's find hook makes of `path`: a copy of the game, or nothing.
Found hook_find(const Game& g, const std::string& path) {
  Found f;
  try {
    if (!g.find || !g.find(path, &f)) f = Found{};
  } catch (...) {
    f = Found{};
  }
  if (f.game != g.id) f = Found{};
  if (f.kind != Found::kNothing && f.path.empty()) f.path = path;
  return f;
}

}  // namespace

std::string what_to_pick(const Game& g) {
  if (!g.pick_words.empty()) return g.pick_words;
  const std::string program = g.program.empty() ? "its program" : g.program;
  std::string s = has_disc_clue(g.rules) ? "a disc image, " : "";
  if (archives_supported()) s += "a download, ";
  return s + "or " + program;
}

std::string find_words(const Game& g) {
  const bool discs = has_disc_clue(g.rules), downloads = archives_supported();
  std::string show = discs ? (downloads ? "your disc, a disc image or download (.iso, .cue, .zip), or the folder where it is installed"
                                        : "your disc, a disc image (.iso, .cue), or the folder where it is installed")
                           : (downloads ? "a download (.zip, .7z) or the folder where it is installed" : "the folder where it is installed");
  if (!g.find_show.empty()) show = g.find_show;
  // Public words name no "we" or "us" (2026-11-27).
  return g.title + " needs your own copy of the game. Pick " + show +
         ". What the game needs is copied into your user folder; your own copy is left untouched.";
}

std::string found_words(const Game& g, const Found& f) {
  if (g.describe) return g.describe(f);
  if (f.kind == Found::kDisc) return "The " + g.title + " disc.";
  return "An installed copy of " + g.title + (f.version.empty() ? std::string() : ", version " + f.version) + ".";
}

std::string megabytes(uint64_t bytes) {
  char buf[32];
  const double mb = static_cast<double>(bytes) / 1e6;
  if (mb < 1) std::snprintf(buf, sizeof buf, "%.0f KB", static_cast<double>(bytes) / 1e3);
  else std::snprintf(buf, sizeof buf, mb < 10 ? "%.1f MB" : "%.0f MB", mb);
  return buf;
}

std::string find_sub(const Game& g) {
  const std::string program = g.program.empty() ? "the game's program" : g.program;
  return !g.find_sub.empty()      ? g.find_sub
         : has_disc_clue(g.rules) ? "A disc image, a download (.zip .iso), or " + program + " in an installed copy"
         : archives_supported()   ? "A download (.zip), or " + program + " in an installed copy"
                                  : program + " in an installed copy";
}

std::string search_copy(const Game& g, const std::string& program_dir) {
  if (g.search) return g.search();
  for (const std::string& place : install_candidates(g.hints, program_dir)) {
    const Found f = find_source(place, g.rules);
    if (f.kind != Found::kNothing && f.game == g.id) return place;
  }
  return {};
}

void pick_source(const Game& g, const std::string& path, std::string* source, std::string* file) {
  *source = path, file->clear();
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return;
  const std::string name = lower(fs::path(path).filename().string());
  const bool program = std::find(g.rules.programs.begin(), g.rules.programs.end(), name) != g.rules.programs.end();
  const bool image = name.size() > 4 && (name.compare(name.size() - 4, 4, ".iso") == 0 || name.compare(name.size() - 4, 4, ".cue") == 0 ||
                                         name.compare(name.size() - 4, 4, ".bin") == 0 || name.compare(name.size() - 4, 4, ".img") == 0);
  if (program || image || archive_name(name)) return;
  *source = fs::path(path).parent_path().string();  // find_source takes the recipe's programs as their folder already
  if (g.find) *file = path;
}

InstallResult look(const Game& g, const std::string& path, const std::string& file, Found* found, const ProgressFn& progress) {
  const std::string name = fs::path(file.empty() ? path : file).filename().string();
  *found = Found{};
  if (!file.empty()) *found = hook_find(g, file);
  if (found->kind == Found::kNothing) {
    *found = find_source(path, g.rules, [&](double at, const std::string& entry) {
      return progress(Progress{"Looking into " + (name.empty() ? path : name), at, entry});
    });
    if (g.find && !found->cancelled && (found->kind == Found::kNothing || found->game != g.id)) {
      const Found other = hook_find(g, path);
      if (other.kind != Found::kNothing) *found = other;
    }
  }
  InstallResult r;
  r.cancelled = found->cancelled;
  r.ok = found->kind != Found::kNothing && found->game == g.id;
  std::string known;  // the game's own words for what was picked
  if (!r.ok && !r.cancelled && g.recognise) {
    try {
      if (!file.empty()) known = g.recognise(file);
      if (known.empty()) known = g.recognise(path);
    } catch (...) {
      known.clear();
    }
  }
  if (!known.empty()) {
    r.summary = known;
  } else if (!r.ok && !r.cancelled) {
    r.summary = "No copy of " + g.title + " was found in " + (name.empty() ? path : name) + ".";
    const std::string why = found->detail.substr(0, found->detail.find('\n'));
    const size_t at = why.find_first_not_of(' ');
    if (at != std::string::npos) {
      r.summary += " " + why.substr(at);
      if (std::strchr(".!?", r.summary.back()) == nullptr) r.summary += ".";
    }
  }
  if (r.cancelled) r.summary = "Stopped. Nothing was changed.";
  return r;
}

std::string out_dir(const std::string& folder, const std::string& default_out) {
  return folder.empty() ? default_out : (fs::path(folder) / "install").make_preferred().string();
}

InstallResult install(const Game& g, const Found& found, const std::string& folder, const SetupOptions& o, const ProgressFn& progress) {
  InstallResult r;
  std::string error;
  if (!g.install) return r.summary = "Nothing was copied: this game cannot install from here.", r;
  if (!folder.empty() && !claim_game_folder(folder, g.folder_key, &error)) return r.summary = "Nothing was copied: " + error + ".", r;
  const std::string out = out_dir(folder, o.default_out);
  r = g.install(found, out, g.media ? g.media(out, o.user_folder) : media_dir(out, o.user_folder), progress);
  if (r.ok && r.install_dir.empty()) r.install_dir = out;
  // The record CHECK MY FILES reads (manifest.h), in the folder this install made only. Without it the menu says it
  // has nothing to check against; the install stands either way.
  std::error_code ec;
  if (r.ok && fs::weakly_canonical(r.install_dir, ec) == fs::weakly_canonical(out, ec)) {
    std::string why;
    write_manifest(out, {}, [&progress](int, int, const std::string& path) {
      if (progress) progress({"Recording the files", 1.0, path});
      return true;
    }, &why);
  }
  return r;
}

}  // namespace rekit::sdl::detail
