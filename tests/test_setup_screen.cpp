// The setup screen (rekit/sdl/setup_screen.h), hidden on SDL's dummy driver: no window is ever shown and no
// picker ever opens (picker_opener is a counter that must stay at 0). Each run ends at a fixed count of frames or
// at its walk's end. A fake game ("Demo Game") is found, offered, installed, refused.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "check.h"
#include "rekit/archive.h"
#include "rekit/game_folder.h"
#include "rekit/hash.h"
#include "rekit/sdl/pickers.h"
#include "rekit/sdl/setup_screen.h"
#include "rekit/sdl/setup_original.h"
#include "test_keys.h"

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
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");  // a window and the software renderer everywhere (offscreen needs GL on a Mac)
  auto keep_un = rekit::sdl::unattended();
  auto keep_open = rekit::sdl::picker_opener();
  rekit::sdl::unattended() = [] { return true; };
  rekit::sdl::picker_opener() = [&run](const rekit::sdl::PickerAsk&) { ++run.pickers; };
  const SetupResult r = rekit::sdl::run_setup(g, t, o);
  rekit::sdl::unattended() = keep_un;
  rekit::sdl::picker_opener() = keep_open;
  if (const char* v = SDL_getenv("REKIT_SETUP_TEST_LOG"); v && *v) run.dump();  // each run's log lines, when asked
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

// The game's recognise hook names what a pick without the game holds; an empty answer or a throw keeps the kit's
// sentence word for word (unset: setup_screen_not_the_game_is_not_offered above).
TEST(setup_screen_recognise_hook_names_the_pick) {
  const std::string kit_words = "No copy of Demo Game was found in junk-copy. demo.dat: not the demo's.";
  const std::string copy = a_copy("junk-copy", "JUNK");
  for (int answer = 0; answer < 3; ++answer) {
    Run run;
    Env env;
    env.set("REKIT_SETUP_GO", "1");
    env.set("REKIT_SETUP_FRAMES", "3");
    SetupOptions o = options(run, tmp("u-recognise" + std::to_string(answer)));
    o.look_first = copy;
    Game g = demo(run);
    std::string asked;
    g.recognise = [answer, &asked](const std::string& path) -> std::string {
      asked = path;
      if (answer == 1) return "";
      if (answer == 2) throw std::runtime_error("unreadable");
      return "The <b>other</b> version of Demo Game was found, but it can't be played yet.";
    };
    const SetupResult r = go(run, g, Theme{}, o);
    EXPECT(r.exit == rekit::kSetupCancelled && run.installs == 0 && !fs::exists(o.default_out));
    EXPECT(asked == copy);
    if (answer == 0) {
      EXPECT(run.has("The <b>other</b> version of Demo Game was found, but it can't be played yet.") && !run.has(kit_words));
    } else {
      EXPECT(run.has(kit_words));
    }
    EXPECT(!run.starts("screen confirm"));
  }
}

// The game's find hook: a copy the kit's walk can't read is offered and installed; its folder's file is asked about
// itself first; a throw, a refusal and another game's find are no find; without it a file is its folder.
namespace {
Game finder(Run& run, std::vector<std::string>& asked, int answer = 0) {
  Game g = demo(run);
  g.find = [&asked, answer](const std::string& path, rekit::Found* f) {
    asked.push_back(path);
    if (fs::path(path).extension() != ".adf" && !fs::exists(fs::path(path) / "demo.adf")) return false;
    f->kind = rekit::Found::kInstall, f->game = answer == 3 ? "other" : "demo", f->version = "disk";
    if (answer == 1) return false;
    if (answer == 2) throw std::runtime_error("unreadable");
    return true;
  };
  g.install = [&run](const rekit::Found& f, const std::string& out, const std::string&, const rekit::sdl::ProgressFn&) {
    ++run.installs, run.out = f.path;
    rekit::sdl::InstallResult r;
    r.ok = true, r.install_dir = out, r.summary = "Demo Game is installed.";
    return r;
  };
  return g;
}
}  // namespace

TEST(setup_screen_find_hook_offers_a_picked_file) {
  const std::string folder = tmp("adf-copy"), adf = (fs::path(folder) / "demo.adf").string();
  put(adf, "a disk");
  for (int answer = 0; answer < 5; ++answer) {  // 4: no hook
    Run run;
    Env env;
    env.set("REKIT_SETUP_FIND", "1");
    env.set("REKIT_SETUP_PICKER", "path:" + adf);
    env.set("REKIT_SETUP_GO", "1");
    env.set("REKIT_SETUP_FRAMES", "40");
    std::vector<std::string> asked, named;
    Game g = finder(run, asked, answer);
    if (answer == 4) g.find = nullptr;
    g.recognise = [&named](const std::string& path) { return named.push_back(path), std::string(); };
    const SetupResult r = go(run, g, Theme{}, options(run, tmp("u-find" + std::to_string(answer))));
    if (answer == 0) {
      EXPECT(r.exit == rekit::kSetupReady && run.installs == 1 && run.out == adf);
      EXPECT(asked == std::vector<std::string>{adf} && named.empty());
      EXPECT(run.has("found plate: demo.adf (An installed copy of Demo Game, version disk.)"));
      continue;
    }
    EXPECT(r.exit == rekit::kSetupCancelled && run.installs == 0);
    if (answer < 4) {
      EXPECT((asked == std::vector<std::string>{adf, folder}) && (named == std::vector<std::string>{adf, folder}));
      EXPECT(run.starts("No copy of Demo Game was found in demo.adf."));
    } else {
      EXPECT(asked.empty() && named == std::vector<std::string>{folder});
      EXPECT(run.starts("No copy of Demo Game was found in adf-copy."));
    }
  }
}

TEST(setup_screen_find_hook_after_the_walk) {
  const std::string folder = tmp("adf-only");
  put(fs::path(folder) / "demo.adf", "a disk");
  {  // a folder the walk finds nothing in: the hook's find, the folder its path
    Run run;
    Env env;
    env.set("REKIT_SETUP_GO", "1");
    env.set("REKIT_SETUP_FRAMES", "40");
    std::vector<std::string> asked;
    SetupOptions o = options(run, tmp("u-find-folder"));
    o.look_first = folder;
    const SetupResult r = go(run, finder(run, asked), Theme{}, o);
    EXPECT(r.exit == rekit::kSetupReady && run.installs == 1 && run.out == folder && asked == std::vector<std::string>{folder});
  }
  {  // a copy the walk finds is the walk's: the hook isn't asked
    Run run;
    Env env;
    env.set("REKIT_SETUP_GO", "1");
    env.set("REKIT_SETUP_FRAMES", "40");
    std::vector<std::string> asked;
    SetupOptions o = options(run, tmp("u-find-walk"));
    o.look_first = a_copy("demo-copy-find");
    const SetupResult r = go(run, finder(run, asked), Theme{}, o);
    EXPECT(r.exit == rekit::kSetupReady && run.installs == 1 && asked.empty());
  }
}

// DOWNLOAD THE GAME FILES: offered only when the game's catalog lists sources for it; the first whole, matching one
// looked at and installed as a pick, the next tried after a bad one, the file deleted after, and a folder of that name
// that is not the kit's never written to. file:/// sources stand in for https:// ones (REKIT_SETUP_ALLOW_FILE).
TEST(setup_screen_downloads_the_original_files) {
  const std::string disk = "a disk", sum = rekit::sha256_hex(reinterpret_cast<const uint8_t*>(disk.data()), disk.size());
  const std::string served = tmp("original-served");
  put(fs::path(served) / "demo.adf", disk);
  const auto src = [&](const std::string& file, const std::string& s, size_t size) {
    return "{\"url\": \"file://" + (fs::path(served) / file).generic_string() + "\", \"sha256\": \"" + s + "\", \"size\": " +
           std::to_string(size) + ", \"name\": \"demo.adf\", \"note\": \"The demo's disk\"}";
  };
  struct Case {
    const char* name;
    std::string original;  // the entry's "original" ("": none)
    bool installed;
    std::string said;  // a log line
    bool foreign = false, configured = true, offered = true;
    int signer = 1;           // the test key the catalog's .sig is by (the game's is 1); 0: no .sig
    bool after_bad = false;   // an unsigned copy of it asked first, passed over
  };
  const std::string bad_sum(64, 'a');
  const std::vector<Case> cases = {
      {"good", "[" + src("demo.adf", sum, disk.size()) + "]", true, "original: downloaded demo.adf"},
      {"sum", "[" + src("demo.adf", bad_sum, disk.size()) + "]", false,
       "status The game files could not be downloaded (it does not match its SHA-256). FIND THE GAME to pick your own copy."},
      {"cut", "[" + src("demo.adf", sum, disk.size() + 4) + "]", false, "original: not downloaded: it is 6 bytes, not 10"},
      {"next", "[" + src("gone.adf", sum, disk.size()) + ", " + src("demo.adf", sum, disk.size()) + "]", true, "original: downloaded demo.adf"},
      {"none", "", false, "original: none offered: the catalog lists no source of the game's files", false, true, false},
      {"http", "[{\"url\": \"http://a.example/d.adf\", \"sha256\": \"" + sum + "\", \"size\": 6, \"name\": \"demo.adf\"}]", false,
       "original: none offered: its original[0]: http://a.example/d.adf is not an https:// address", false, true, false},
      {"foreign", "[" + src("demo.adf", sum, disk.size()) + "]", false, "is not the kit's folder, so nothing was written to it", true, true, false},
      {"unasked", "[" + src("demo.adf", sum, disk.size()) + "]", false, "", false, false, false},
      {"unsigned", "[" + src("demo.adf", sum, disk.size()) + "]", false, "", false, true, false, 0},
      {"wrong-key", "[" + src("demo.adf", sum, disk.size()) + "]", false, "", false, true, false, 3},
      {"second-address", "[" + src("demo.adf", sum, disk.size()) + "]", true, "original: downloaded demo.adf", false, true, true, 1, true},
  };
  for (const Case& c : cases) {
    std::printf("  %s\n", c.name);
    Run run;
    Env env;
    env.set("REKIT_SETUP_ALLOW_FILE", "1");
    env.set("REKIT_SETUP_DOWNLOAD", "1");  // no REKIT_SETUP_GO: a download installs without the confirm
    env.set("REKIT_SETUP_FRAMES", "40");
    const std::string args_file = tmp(std::string("original-args-") + c.name + ".txt");
    fs::remove(args_file);
    env.set("REKIT_CURL_ARGS_TO", args_file.c_str());
    const std::string temp = tmp(std::string("original-tmp-") + c.name), cat = tmp(std::string("original-") + c.name + ".json");
    const fs::path dir = fs::path(temp) / "rekit-original-demo";
    if (c.foreign) put(dir / "mine.txt", "mine");
    const std::string text = "{\"revision\": 4, \"games\": [{\"key\": \"demo\", \"name\": \"Demo Game\"" +
                             (c.original.empty() ? std::string() : ", \"original\": " + c.original) + "}]}";
    put(cat, text);
    if (c.signer) put(cat + ".sig", test_keys::sign(text, static_cast<uint8_t>(c.signer)));
    const std::string bad_cat = cat + ".unsigned.json";
    put(bad_cat, text);  // no .sig beside it
    std::vector<std::string> asked;
    Game g = finder(run, asked);
    if (c.configured) {
      if (c.after_bad) g.original_catalogs.push_back("file://" + fs::path(bad_cat).generic_string());
      g.original_catalogs.push_back("file://" + fs::path(cat).generic_string());
      g.original_key = "demo", g.catalog_keys = {test_keys::public_key(1)};
    }
    SetupOptions o = options(run, tmp(std::string("u-original-") + c.name));
    o.temp_dir = temp;
    const SetupResult r = go(run, g, Theme{}, o);
    EXPECT(r.exit == (c.installed ? rekit::kSetupReady : rekit::kSetupCancelled) && run.installs == (c.installed ? 1 : 0));
    const std::string said = c.foreign ? "original: none offered: " + dir.string() + " " + c.said : c.said;
    EXPECT(said.empty() || run.has(said));
    // A fresh setup with a catalog searches first (none here) and asks about the download only when one is offered.
    EXPECT(c.configured == run.has("start: no copy found on this computer"));
    EXPECT(run.has(c.offered ? "screen find: DOWNLOAD THE GAME FILES, PICK MY OWN COPY, QUIT" : "screen find: FIND THE GAME, QUIT"));
    EXPECT(c.offered == run.has("work: GETTING THE ORIGINAL GAME FILES") && c.configured == run.starts("original: "));
    if (c.installed) EXPECT(run.out == (dir / "demo.adf").string() && run.has("original: demo.adf deleted"));
    EXPECT(!run.starts("screen confirm") && c.installed == run.has("work: INSTALLING DEMO GAME"));
    if (c.installed) EXPECT(r.install_dir == o.default_out);
    if (c.foreign) {
      EXPECT(fs::exists(dir / "mine.txt") && !fs::exists(dir / "demo.adf") && !fs::exists(dir / "rekit-original-download.txt"));
    } else {
      EXPECT(!fs::exists(dir));  // the download and its folder gone, whatever happened
    }
    EXPECT(run.pickers == 0);
    if (std::string(c.name) == "good") {  // the catalog and its .sig asked with no-cache headers, the download without
      std::ifstream af(args_file);
      const std::string a((std::istreambuf_iterator<char>(af)), std::istreambuf_iterator<char>());
      const auto fresh = [&](const std::string& u) {
        const size_t at = a.find("\n" + u + "\n"), from = at == std::string::npos ? at : a.rfind("re-setup-kit\n", at);
        return at != std::string::npos && from != std::string::npos &&
               a.substr(from, at - from + 1).find("\n--header\nCache-Control: no-cache\n--header\nPragma: no-cache\n") != std::string::npos;
      };
      const std::string cu = "file://" + fs::path(cat).generic_string();
      EXPECT(fresh(cu) && fresh(cu + ".sig") && a.find("demo.adf\n") != std::string::npos && !fresh("file://" + (fs::path(served) / "demo.adf").generic_string()));
    }
    if (c.signer == 0) EXPECT(run.starts("original: none offered: file://" + fs::path(cat).generic_string() + ": no signature ("));
    if (c.signer == 3) EXPECT(run.has("original: none offered: file://" + fs::path(cat).generic_string() + ": its signature does not check against the game's keys"));
    if ((!said.empty() && !run.has(said)) || std::string(c.name) == "good") run.dump();
  }
}

// The game's own words for what to pick, and the kit's without them.
TEST(setup_screen_pick_words) {
  for (const bool own : {false, true}) {
    Run run;
    Env env;
    env.set("REKIT_SETUP_FIND", "1");
    env.set("REKIT_SETUP_PICKER", "cancel");
    env.set("REKIT_SETUP_FRAMES", "3");
    Game g = demo(run);
    if (own) g.pick_words = "a disk image, or DEMO.EXE", g.find_sub = "A disk image (.adf)", g.find_show = "a disk image (.adf)";
    go(run, g, Theme{}, options(run, tmp("u-words")));
    const std::string kit = std::string(rekit::archives_supported() ? "a download, " : "") + "or DEMO.EXE";
    EXPECT(run.has("status Not found on this computer. Pick it: " + (own ? g.pick_words : kit) + "."));
  }
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
    EXPECT(run.has("status Not found on this computer. Pick it: a download, or DEMO.EXE.") || !rekit::archives_supported());
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
    std::string last;
    for (const std::string& l : run.log)
      if (l.rfind("status ", 0) == 0) last = l;
    EXPECT(last == "status (none)");  // the find screen's "Not found ... Pick it" gone once a find is offered
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

// The files in use: what is wrong with them said; picking them again says so and stays (same_words), another copy is
// offered as ever. Without same_words the game's own copy still ends the setup, ready.
TEST(setup_screen_says_the_files_are_the_ones_in_use) {
  for (const bool own : {false, true}) {  // in use where the player keeps them; the game's own copy
    for (const bool says : {true, false}) {
      Run run;
      Env env;
      env.set("REKIT_SETUP_FIND", "1");
      env.set("REKIT_SETUP_FRAMES", "60");
      SetupOptions o = options(run, tmp(std::string("u-same") + (own ? "o" : "i") + (says ? "s" : "n")));
      o.in_use = a_copy("same-in-use");
      if (own) put(fs::path(o.default_out) / "demo.dat", "DEMO v2"), o.in_use = o.default_out;
      o.notice = "DEMO.DAT is damaged.";
      if (says) o.same_words = "Those are the files in use. Pick another copy.";
      env.set("REKIT_SETUP_PICKER", "path:" + o.in_use);
      const SetupResult r = go(run, demo(run), Theme{}, o);
      EXPECT(run.has("notice DEMO.DAT is damaged.") && run.installs == 0);
      EXPECT(run.has("status Those are the files in use. Pick another copy.") == says);
      EXPECT(run.starts("found plate") == (!says && !own));
      EXPECT((r.exit == rekit::kSetupReady) == (!says && own));
    }
  }
  {  // in use elsewhere, the game's own copy picked to go back to it: ready, as without same_words
    Run run;
    Env env;
    env.set("REKIT_SETUP_FIND", "1");
    env.set("REKIT_SETUP_FRAMES", "60");
    SetupOptions o = options(run, tmp("u-same-back"));
    put(fs::path(o.default_out) / "demo.dat", "DEMO v2");
    o.in_use = a_copy("same-in-use"), o.same_words = "x";
    env.set("REKIT_SETUP_PICKER", "path:" + o.default_out);
    const SetupResult r = go(run, demo(run), Theme{}, o);
    EXPECT(r.exit == rekit::kSetupReady && r.install_dir == o.default_out && run.installs == 0);
  }
  Run run;
  Env env;
  env.set("REKIT_SETUP_FIND", "1");
  env.set("REKIT_SETUP_FRAMES", "60");
  SetupOptions o = options(run, tmp("u-same-other"));
  o.in_use = a_copy("same-in-use"), o.same_words = "x";
  env.set("REKIT_SETUP_PICKER", "path:" + a_copy("same-other"));
  go(run, demo(run), Theme{}, o);
  EXPECT(run.starts("found plate: same-other") && !run.starts("notice"));
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
  const char* dir = SDL_getenv("REKIT_SETUP_TEST_SHOTS");
  if (!dir || !*dir) return;
  Theme t;
  if (const char* font = SDL_getenv("REKIT_SETUP_TEST_FONT")) t.font = font;
  if (const char* badge = SDL_getenv("REKIT_SETUP_TEST_BADGE")) t.badge = badge;
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

// A hidden run with no driver asked for takes the dummy one (no display needed: ssh, CI), as a hint only: the
// game's own choice is left as it was afterwards.
TEST(setup_screen_hidden_needs_no_display) {
  if (SDL_getenv("SDL_VIDEO_DRIVER")) return;  // a driver named for the whole run: that one is used
  Run run;
  Env env;
  env.set("REKIT_SETUP_FRAMES", "2");
  SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
  auto keep_un = rekit::sdl::unattended();
  rekit::sdl::unattended() = [] { return true; };
  const SetupResult r = rekit::sdl::run_setup(demo(run), Theme{}, options(run, tmp("u-nodisplay")));
  rekit::sdl::unattended() = keep_un;
  EXPECT(r.exit == rekit::kSetupCancelled && run.has("frames 2: ended") && run.has("video dummy"));
  EXPECT(SDL_GetHint(SDL_HINT_VIDEO_DRIVER) == nullptr);  // not left behind for the game
  // A driver the game asked for stays its own.
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Run again;
  rekit::sdl::run_setup(demo(again), Theme{}, options(again, tmp("u-nodisplay2")));
  EXPECT(SDL_GetHint(SDL_HINT_VIDEO_DRIVER) && std::string(SDL_GetHint(SDL_HINT_VIDEO_DRIVER)) == "dummy");
}

// A fresh setup with a catalog searches first (2026-11-27): a copy on this computer is offered as FIND THE GAME's
// would be, and the download is never asked about, even with the catalog offering one and DOWNLOAD set to be pressed.
// Without a copy the find screen asks: DOWNLOAD THE GAME FILES (its size and source) or PICK MY OWN COPY.
TEST(setup_screen_searches_before_it_offers_a_download) {
  const std::string disk = "a disk", sum = rekit::sha256_hex(reinterpret_cast<const uint8_t*>(disk.data()), disk.size());
  const std::string served = tmp("first-served"), copy = tmp("first-copy");
  put(fs::path(served) / "demo.adf", disk);
  put(fs::path(copy) / "demo.adf", disk);
  const std::string text = "{\"revision\": 4, \"games\": [{\"key\": \"demo\", \"name\": \"Demo Game\", \"original\": [{\"url\": \"file://" +
                           (fs::path(served) / "demo.adf").generic_string() + "\", \"sha256\": \"" + sum +
                           "\", \"size\": 1400000, \"name\": \"demo.adf\", \"note\": \"the Internet Archive\"}]}]}";
  const std::string cat = tmp("first.json");
  put(cat, text);
  put(cat + ".sig", test_keys::sign(text, 1));
  for (const bool here : {true, false}) {
    Run run;
    Env env;
    env.set("REKIT_SETUP_ALLOW_FILE", "1");
    env.set("REKIT_SETUP_FRAMES", "40");
    if (here) env.set("REKIT_SETUP_DOWNLOAD", "1");  // would press it, were it offered
    std::vector<std::string> asked;
    Game g = finder(run, asked);
    if (here) g.search = [copy] { return copy; };
    g.original_catalogs = {"file://" + fs::path(cat).generic_string()};
    g.original_key = "demo", g.catalog_keys = {test_keys::public_key(1)};
    SetupOptions o = options(run, tmp(std::string("u-first-") + (here ? "here" : "none")));
    o.temp_dir = tmp(std::string("first-tmp-") + (here ? "here" : "none"));
    go(run, g, Theme{}, o);
    if (here) {
      EXPECT(run.has("start: a copy found at " + copy) && !run.has("start: no copy found on this computer"));
      EXPECT(!run.has("work: GETTING THE ORIGINAL GAME FILES") && !run.starts("screen find: DOWNLOAD"));
    } else {
      EXPECT(run.has("start: no copy found on this computer") && run.has("original: 1 sources offered"));
      EXPECT(run.has("screen find: DOWNLOAD THE GAME FILES, PICK MY OWN COPY, QUIT"));
      EXPECT(!run.has("work: GETTING THE ORIGINAL GAME FILES"));
    }
    if (!(here ? run.has("start: a copy found at " + copy) : run.has("start: no copy found on this computer"))) run.dump();
  }
}

#if !defined(_WIN32)
// A dropped download continues where it stopped (2026-11-27): a stand-in server on this computer sends half of a
// 3 MB file and closes the connection; 5 s later the download asks from the part's end. A server that honors the
// Range sends the rest (two requests); one that ignores it (a whole 200) has the download start over (three). Either
// way the file arrives byte for byte.
TEST(setup_original_download_resumes_after_a_dropped_connection) {
  std::string body(3 * 1000 * 1000, '\0');
  for (size_t i = 0; i < body.size(); ++i) body[i] = static_cast<char>((i * 131 + i / 7) & 0xff);
  for (const bool ranges : {true, false}) {
    const int srv = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET, a.sin_addr.s_addr = htonl(INADDR_LOOPBACK), a.sin_port = 0;
    EXPECT(srv >= 0 && bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && listen(srv, 4) == 0);
    socklen_t al = sizeof a;
    getsockname(srv, reinterpret_cast<sockaddr*>(&a), &al);
    std::vector<std::string> asked;
    std::thread server([&] {
      for (int n = 0; n < (ranges ? 2 : 3); ++n) {
        const int c = accept(srv, nullptr, nullptr);
        if (c < 0) return;
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos) {
          const ssize_t got = recv(c, buf, sizeof buf, 0);
          if (got <= 0) break;
          req.append(buf, static_cast<size_t>(got));
        }
        asked.push_back(req);
        size_t from = 0;
        if (const size_t r = req.find("Range: bytes="); ranges && r != std::string::npos) from = std::strtoull(req.c_str() + r + 13, nullptr, 10);
        std::string out;
        if (from > 0) {
          out = "HTTP/1.1 206 Partial Content\r\nContent-Length: " + std::to_string(body.size() - from) + "\r\nContent-Range: bytes " +
                std::to_string(from) + "-" + std::to_string(body.size() - 1) + "/" + std::to_string(body.size()) + "\r\n\r\n" + body.substr(from);
        } else {
          out = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + (n == 0 ? body.substr(0, body.size() / 2) : body);
        }
        for (size_t at = 0; at < out.size();) {
          const ssize_t sent = send(c, out.data() + at, out.size() - at, 0);
          if (sent <= 0) break;
          at += static_cast<size_t>(sent);
        }
        close(c);
      }
    });
    const std::string to = tmp(std::string("resume-") + (ranges ? "ranges" : "whole") + ".bin");
    fs::remove(to);
    Env env;
    env.set("REKIT_LOCAL_HTTP", "1");
    std::string error;
    bool cancelled = false;
    const bool ok = rekit::sdl::fetch_url("http://127.0.0.1:" + std::to_string(ntohs(a.sin_port)) + "/d.zip", to, body.size(), 0, false,
                                          [](double) { return true; }, &error, &cancelled);
    shutdown(srv, SHUT_RDWR), close(srv);
    server.join();
    std::ifstream f(to, std::ios::binary);
    const std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::printf("  %s: %s, %zu requests%s%s\n", ranges ? "ranges" : "whole", ok ? "whole" : "failed", asked.size(), error.empty() ? "" : ": ", error.c_str());
    EXPECT(ok && !cancelled && got == body && !fs::exists(to + ".part"));
    EXPECT(asked.size() == (ranges ? 2u : 3u));
    if (ranges) EXPECT(asked.size() > 1 && asked[1].find("Range: bytes=" + std::to_string(body.size() / 2) + "-") != std::string::npos);
  }
}
#endif
