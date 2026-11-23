// The setup screen (rekit/sdl/setup_screen.h), hidden on SDL's offscreen driver: no window is ever shown and no
// picker ever opens (picker_opener is a counter that must stay at 0). Each run ends at a fixed count of frames or
// at its walk's end. A fake game ("Demo Game") is found, offered, installed, refused.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/game_folder.h"
#include "rekit/sdl/pickers.h"
#include "rekit/sdl/setup_screen.h"

namespace fs = std::filesystem;
using rekit::sdl::Game;
using rekit::sdl::SetupOptions;
using rekit::sdl::SetupResult;
using rekit::sdl::Theme;

namespace {

void put(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}
std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
// A path as the screen shows it: the system's separators.
std::string native(const fs::path& p) { return fs::path(p).make_preferred().string(); }
std::string tmp(const std::string& name) { return (fs::path(REKIT_TEST_TMP) / "setup-screen" / name).string(); }

// REKIT_SETUP_* set in SDL's environment (SDL_getenv reads SDL's copy) for one run, cleared after.
struct Env {
  std::vector<std::string> names;
  void set(const char* name, const std::string& value) {
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value.c_str(), true);
    names.push_back(name);
  }
  ~Env() {
    for (const std::string& n : names) SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), n.c_str());
  }
};

struct Run {
  std::vector<std::string> log;
  int installs = 0, pickers = 0;
  std::string out, media;
  bool has(const std::string& line) const {
    for (const std::string& l : log)
      if (l == line) return true;
    return false;
  }
  bool starts(const std::string& head) const {
    for (const std::string& l : log)
      if (l.rfind(head, 0) == 0) return true;
    return false;
  }
  void dump() const {
    for (const std::string& l : log) std::printf("    | %s\n", l.c_str());
  }
};

Game demo(Run& run, bool install_fails = false) {
  Game g;
  g.id = "demo", g.title = "Demo Game", g.folder_key = "demo", g.folder_name = "Demo Game", g.program = "DEMO.EXE";
  g.rules.clues.push_back({rekit::Clue::kInstall, "demo.dat", 1u << 20, [](const std::vector<uint8_t>& b) {
                             rekit::Identity id;
                             if (b.size() >= 4 && std::string(b.begin(), b.begin() + 4) == "DEMO") id.game = "demo", id.version = "2", id.rank = 5;
                             else id.why = "not the demo's";
                             return id;
                           }});
  g.rules.programs = {"demo.exe"};
  g.search = [] { return std::string(); };  // nothing found on this computer: the picker
  g.install = [&run, install_fails](const rekit::Found& f, const std::string& out, const std::string& media,
                                    const rekit::sdl::ProgressFn& progress) {
    ++run.installs, run.out = out, run.media = media;
    rekit::sdl::InstallResult r;
    progress({"Copying the game files", 0.5, "demo.dat"});
    if (install_fails) return r.summary = "Nothing was installed: the disk is full.", r;
    std::error_code ec;
    fs::create_directories(out, ec);
    fs::copy_file(fs::path(f.path) / "demo.dat", fs::path(out) / "demo.dat", fs::copy_options::overwrite_existing, ec);
    r.ok = !ec, r.install_dir = out, r.summary = "Demo Game is installed.";
    return r;
  };
  return g;
}

SetupOptions options(Run& run, const std::string& user) {
  SetupOptions o;
  o.user_folder = user;
  o.default_out = (fs::path(user) / "install").string();
  o.hidden = true;
  o.log = [&run](const std::string& l) { run.log.push_back(l); };
  return o;
}

SetupResult go(Run& run, const Game& g, const Theme& t, const SetupOptions& o) {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  auto keep_un = rekit::sdl::unattended();
  auto keep_open = rekit::sdl::picker_opener();
  rekit::sdl::unattended() = [] { return true; };
  rekit::sdl::picker_opener() = [&run](const rekit::sdl::PickerAsk&) { ++run.pickers; };
  const SetupResult r = rekit::sdl::run_setup(g, t, o);
  rekit::sdl::unattended() = keep_un;
  rekit::sdl::picker_opener() = keep_open;
  if (const char* v = std::getenv("REKIT_SETUP_TEST_LOG"); v && *v) run.dump();  // each run's log lines, when asked
  return r;
}

std::string a_copy(const std::string& name, const std::string& bytes = "DEMO v2") {
  const std::string d = tmp(name);
  put(fs::path(d) / "demo.dat", bytes);
  put(fs::path(d) / "DEMO.EXE", "MZ");
  return d;
}

}  // namespace

TEST(setup_screen_hidden_ends_at_its_frames) {
  Run run;
  Env env;
  env.set("REKIT_SETUP_FRAMES", "3");
  const SetupResult r = go(run, demo(run), Theme{}, options(run, tmp("u-frames")));
  EXPECT(r.exit == rekit::kSetupCancelled && run.pickers == 0 && run.installs == 0);
  EXPECT(run.has("screen find: FIND THE GAME, QUIT") && run.has("header DEMO GAME") && run.has("frames 3: ended"));
  EXPECT(!fs::exists(tmp("u-frames")));  // nothing written by looking
}

TEST(setup_screen_theme_header_and_bad_art) {
  Run run;
  Env env;
  env.set("REKIT_SETUP_FRAMES", "2");
  Theme t;
  t.header = "DEMO RELOADED";
  t.badge = tmp("no-such-badge.png");
  put(tmp("not-a.png"), "not a png");
  t.plate = tmp("not-a.png"), t.plate_slice = 8;
  const SetupResult r = go(run, demo(run), t, options(run, tmp("u-theme")));
  EXPECT(r.exit == rekit::kSetupCancelled && run.has("header DEMO RELOADED"));
  EXPECT(run.has("theme: " + t.badge + " could not be read") && run.has("theme: " + t.plate + " could not be read"));
}

TEST(setup_screen_offers_a_find_and_installs) {
  Run run;
  Env env;
  env.set("REKIT_SETUP_GO", "1");
  env.set("REKIT_SETUP_FRAMES", "50");  // a cap: the walk ends ready before it
  SetupOptions o = options(run, tmp("u-install"));
  o.look_first = a_copy("demo-copy");
  const SetupResult r = go(run, demo(run), Theme{}, o);
  EXPECT(r.exit == rekit::kSetupReady && r.install_dir == o.default_out && run.installs == 1);
  EXPECT(read(fs::path(o.default_out) / "demo.dat") == "DEMO v2" && run.media == o.user_folder);
  EXPECT(run.has("work: LOOKING AT YOUR GAME") && run.has("screen confirm: demo-copy, INSTALL, CHANGE FOLDER, BACK"));
  EXPECT(run.has("game files into " + o.default_out) && run.has("work: INSTALLING DEMO GAME") && run.pickers == 0);
}

TEST(setup_screen_change_folder_makes_the_game_folder) {
  Run run;
  Env env;
  const std::string picked = tmp("drive");
  put(fs::path(picked) / "someone.txt", "theirs");
  env.set("REKIT_SETUP_FILES_TO", picked);
  env.set("REKIT_SETUP_GO", "1");
  env.set("REKIT_SETUP_FRAMES", "50");
  SetupOptions o = options(run, tmp("u-folder"));
  o.look_first = a_copy("demo-copy2");
  const SetupResult r = go(run, demo(run), Theme{}, o);
  const fs::path game = fs::path(picked) / "Demo Game";
  EXPECT(r.exit == rekit::kSetupReady && r.install_dir == native(game / "install"));
  EXPECT(read(game / rekit::kGameFolderMark).find("\ngame=demo\n") != std::string::npos && fs::exists(game / rekit::kGamesFolderMark));
  EXPECT(run.media == native(game) && read(fs::path(picked) / "someone.txt") == "theirs");
  EXPECT(run.has("Demo Game goes into " + native(game)) && !fs::exists(o.default_out));
}

// Break: the game folder made inside the pick already holds someone's files: refused, nothing copied.
TEST(setup_screen_refuses_a_folder_not_the_games) {
  Run run;
  Env env;
  const std::string picked = tmp("drive2");
  put(fs::path(picked) / "Demo Game" / "photo.jpg", "theirs");
  env.set("REKIT_SETUP_FILES_TO", picked);
  env.set("REKIT_SETUP_GO", "1");
  env.set("REKIT_SETUP_FRAMES", "3");
  SetupOptions o = options(run, tmp("u-refuse"));
  o.look_first = a_copy("demo-copy3");
  const SetupResult r = go(run, demo(run), Theme{}, o);
  const fs::path game = fs::path(picked) / "Demo Game";
  EXPECT(r.exit == rekit::kSetupCancelled && run.installs == 0);
  EXPECT(run.has("Nothing was copied: " + native(game) + " holds files that are not this game's: pick an empty folder."));
  EXPECT(!fs::exists(game / rekit::kGameFolderMark) && read(game / "photo.jpg") == "theirs");
}

// Break: a folder without the game is not offered.
TEST(setup_screen_not_the_game_is_not_offered) {
  Run run;
  Env env;
  env.set("REKIT_SETUP_GO", "1");
  env.set("REKIT_SETUP_FRAMES", "3");
  SetupOptions o = options(run, tmp("u-notgame"));
  o.look_first = a_copy("junk-copy", "JUNK");
  const SetupResult r = go(run, demo(run), Theme{}, o);
  EXPECT(r.exit == rekit::kSetupCancelled && run.installs == 0 && !fs::exists(o.default_out));
  EXPECT(run.has("No copy of Demo Game was found in junk-copy. demo.dat: not the demo's.") && !run.starts("screen confirm"));
}

// Break: an install that fails leaves the player on the find screen with its words.
TEST(setup_screen_failed_install_goes_back) {
  Run run;
  Env env;
  env.set("REKIT_SETUP_GO", "1");
  env.set("REKIT_SETUP_FRAMES", "3");
  SetupOptions o = options(run, tmp("u-fail"));
  o.look_first = a_copy("demo-copy4");
  const SetupResult r = go(run, demo(run, true), Theme{}, o);
  EXPECT(r.exit == rekit::kSetupCancelled && run.installs == 1);
  EXPECT(run.has("Nothing was installed: the disk is full.") && run.log.size() > 2 &&
         std::count(run.log.begin(), run.log.end(), "screen find: FIND THE GAME, QUIT") == 2);
}

// No picker opens unattended; a check's answer stands in for one.
TEST(setup_screen_pickers_never_open_hidden) {
  {
    Run run;
    Env env;
    env.set("REKIT_SETUP_FIND", "1");
    env.set("REKIT_SETUP_FRAMES", "3");
    const SetupResult r = go(run, demo(run), Theme{}, options(run, tmp("u-pick")));
    EXPECT(r.exit == rekit::kSetupCancelled && run.pickers == 0);
    EXPECT(run.has("the file picker asked for") && run.has("no picker in a run no player is at: cancelled"));
    EXPECT(run.starts("the picker answered (cancelled)"));
  }
  {
    Run run;
    Env env;
    const std::string copy = a_copy("demo-copy5");
    env.set("REKIT_SETUP_FIND", "1");
    env.set("REKIT_SETUP_PICKER", "path:" + (fs::path(copy) / "demo.dat").string());  // a file of the game: its folder
    env.set("REKIT_SETUP_FRAMES", "3");
    const SetupResult r = go(run, demo(run), Theme{}, options(run, tmp("u-pick2")));
    EXPECT(r.exit == rekit::kSetupCancelled && run.pickers == 0 && run.installs == 0);
    EXPECT(run.has("screen confirm: demo-copy5, INSTALL, CHANGE FOLDER, BACK"));
  }
}

TEST(setup_screen_keys_reach_play) {
  Run run;
  Env env;
  env.set("REKIT_SETUP_NAV", "next,next,press");
  env.set("REKIT_SETUP_FRAMES", "3");
  SetupOptions o = options(run, tmp("u-keys"));
  o.in_use = a_copy("in-use"), o.play_button = true;
  const SetupResult r = go(run, demo(run), Theme{}, o);
  EXPECT(r.exit == rekit::kSetupReady && r.install_dir == o.in_use && run.has("screen find: FIND THE GAME, PLAY"));
  EXPECT(run.has("focus FIND THE GAME") && run.has("focus PLAY"));
}

// The frame as drawn: the theme's rule under the header band, its bands, its background.
TEST(setup_screen_draws_the_theme) {
  Run run;
  Env env;
  const std::string shot = tmp("shot.bmp");
  fs::create_directories(fs::path(shot).parent_path());
  env.set("REKIT_SETUP_SHOT", shot);
  Theme t;
  t.bg = {10, 40, 20, 255}, t.band_colour = {60, 20, 20, 255}, t.rule = {0, 200, 255, 255};
  const SetupResult r = go(run, demo(run), t, options(run, tmp("u-shot")));
  EXPECT(r.exit == rekit::kSetupCancelled && run.has("shot " + shot));
  SDL_Surface* s = SDL_LoadBMP(shot.c_str());
  EXPECT(s != nullptr);
  if (!s) return;
  auto at = [&](int x, int y) {
    Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
    SDL_ReadSurfacePixel(s, x * s->w / 854, y * s->h / 480, &cr, &cg, &cb, &ca);
    return (cr << 16) | (cg << 8) | cb;
  };
  EXPECT(at(4, 96) == 0x00C8FF && at(4, 480 - 49) == 0x00C8FF);  // the rules
  EXPECT(at(4, 40) == 0x3C1414 && at(4, 460) == 0x3C1414);        // the bands
  EXPECT(at(4, 300) == 0x0A2814);                                 // the background
  SDL_DestroySurface(s);
}

// REKIT_SETUP_TEST_SHOTS=<dir> (a review, by hand): the screens saved there as BMPs, at 854x480 and at twice that
// (the text drawn at the window's pixels). Pictures are never committed.
TEST(setup_screen_review_shots) {
  const char* dir = std::getenv("REKIT_SETUP_TEST_SHOTS");
  if (!dir || !*dir) return;
  Theme t;
  if (const char* font = std::getenv("REKIT_SETUP_TEST_FONT")) t.font = font;
  if (const char* badge = std::getenv("REKIT_SETUP_TEST_BADGE")) t.badge = badge;
  for (int scale : {1, 2}) {
    const std::string tag = scale == 1 ? "" : "-2x";
    {
      Run run;
      Env env;
      env.set("REKIT_SETUP_SHOT", std::string(dir) + "/find" + tag + ".bmp");
      SetupOptions o = options(run, tmp("u-review"));
      o.width = 854 * scale, o.height = 480 * scale;
      EXPECT(go(run, demo(run), t, o).exit == rekit::kSetupCancelled);
    }
    {
      Run run;
      Env env;
      env.set("REKIT_SETUP_SHOT", std::string(dir) + "/confirm" + tag + ".bmp");
      env.set("REKIT_SETUP_FRAMES", "2");
      SetupOptions o = options(run, tmp("u-review2"));
      o.look_first = a_copy("A Copy Of The Demo Game"), o.width = 854 * scale, o.height = 480 * scale;
      EXPECT(go(run, demo(run), t, o).exit == rekit::kSetupCancelled);
    }
    {
      Run run;
      Env env;
      env.set("REKIT_SETUP_SHOT", std::string(dir) + "/refused" + tag + ".bmp");
      env.set("REKIT_SETUP_FRAMES", "2");
      env.set("REKIT_SETUP_GO", "1");
      SetupOptions o = options(run, tmp("u-review3"));
      o.look_first = a_copy("junk", "JUNK"), o.width = 854 * scale, o.height = 480 * scale;
      EXPECT(go(run, demo(run), t, o).exit == rekit::kSetupCancelled);
    }
  }
}
