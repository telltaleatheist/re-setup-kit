// The setup without a screen (rekit/sdl/setup_serve.h): its lines on stdout, read back, and its exits.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define REKIT_DUP _dup
#define REKIT_DUP2 _dup2
#define REKIT_CLOSE _close
#define REKIT_FILENO _fileno
#else
#include <unistd.h>
#define REKIT_DUP dup
#define REKIT_DUP2 dup2
#define REKIT_CLOSE close
#define REKIT_FILENO fileno
#endif

#include "check.h"
#include "rekit/manifest.h"
#include "rekit/game_folder.h"
#include "rekit/sdl/setup_serve.h"
#include "rekit/status.h"

namespace fs = std::filesystem;
using rekit::SetupMessage;
using rekit::sdl::Game;
using rekit::sdl::SetupOptions;
using rekit::sdl::SetupResult;

namespace {

void put(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}
std::string tmp(const std::string& name) { return (fs::path(REKIT_TEST_TMP) / "setup-serve" / name).string(); }

Game demo(int* installs) {
  Game g;
  g.id = "demo", g.title = "Demo Game", g.folder_key = "demo", g.folder_name = "Demo Game", g.program = "DEMO.EXE";
  g.rules.clues.push_back({rekit::Clue::kInstall, "demo.dat", 1u << 20, [](const std::vector<uint8_t>& b) {
                             rekit::Identity id;
                             if (b.size() >= 4 && std::string(b.begin(), b.begin() + 4) == "DEMO") id.game = "demo", id.version = "2", id.rank = 5;
                             else id.why = "not the demo's";
                             return id;
                           }});
  g.rules.programs = {"demo.exe"};
  g.search = [] { return tmp("found"); };
  g.install = [installs](const rekit::Found& f, const std::string& out, const std::string&, const rekit::sdl::ProgressFn& progress) {
    rekit::sdl::InstallResult r;
    for (int i = 0; i < 40; ++i)
      if (!progress({i < 20 ? "Copying" : "Checking", i / 40.0, "demo.dat"})) return r.cancelled = true, r.summary = "Stopped.", r;
    ++*installs;
    std::error_code ec;
    fs::create_directories(out, ec);
    fs::copy_file(fs::path(f.path) / "demo.dat", fs::path(out) / "demo.dat", fs::copy_options::overwrite_existing, ec);
    r.ok = !ec, r.summary = "Demo Game is installed.";
    return r;
  };
  return g;
}

struct Served {
  SetupResult r;
  SetupMessage last;
  std::vector<SetupMessage> steps;
  int other = 0;  // lines that are not messages
};
// serve_setup with stdout in a file, its lines parsed.
Served ask(const Game& g, const std::vector<std::string>& args, const std::string& user = tmp("user")) {
  Served s;
  SetupOptions o;
  o.user_folder = user, o.default_out = (fs::path(user) / "install").string();
  const std::string file = tmp("out.txt");
  fs::create_directories(fs::path(file).parent_path());
  std::fflush(stdout);
  const int keep = REKIT_DUP(REKIT_FILENO(stdout));
  std::FILE* f = std::fopen(file.c_str(), "wb");
  REKIT_DUP2(REKIT_FILENO(f), REKIT_FILENO(stdout));
  s.r = rekit::sdl::serve_setup(g, o, args);
  std::fflush(stdout);
  REKIT_DUP2(keep, REKIT_FILENO(stdout));
  REKIT_CLOSE(keep), std::fclose(f);
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    SetupMessage m;
    if (!rekit::parse_setup_message(line, &m)) ++s.other;
    else if (m.done) s.last = m;
    else s.steps.push_back(m);
  }
  return s;
}

}  // namespace

TEST(setup_messages_round_trip) {
  SetupMessage m;
  m.fraction = 0.25, m.stage = "Copying \"it\"", m.message = "C:\\x";
  SetupMessage b;
  EXPECT(rekit::parse_setup_message(rekit::setup_message_json(m), &b) && !b.done && b.fraction == 0.25 && b.stage == m.stage && b.message == m.message);
  m = {};
  m.done = m.ok = m.in_place = true, m.path = "/a b", m.words = "w", m.into = "i", m.where = "x", m.summary = "s";
  m.title = "t", m.pick_words = "p", m.find_sub = "f", m.find_words = "q";
  EXPECT(rekit::parse_setup_message(rekit::setup_message_json(m), &b) && b.done && b.ok && b.in_place && !b.cancelled && b.path == m.path &&
         b.words == "w" && b.into == "i" && b.where == "x" && b.summary == "s" && b.title == "t" && b.pick_words == "p" &&
         b.find_sub == "f" && b.find_words == "q");
  for (const char* bad : {"", "setup: font x", "{}", "{\"done\": true", "{\"done\": true} x", "[1]"}) EXPECT(!rekit::parse_setup_message(bad, &b));
  // In the status: told only when set, read back, and not there in an earlier game's.
  rekit::Status st, back;
  st.game = "demo";
  EXPECT(rekit::status_json(st).find("setup") == std::string::npos);
  EXPECT(rekit::parse_status(rekit::status_json(st), &back) && back.setup == 0);
  st.setup = rekit::kServedSetup;
  EXPECT(rekit::parse_status(rekit::status_json(st), &back) && back.setup == rekit::kServedSetup);
}

TEST(setup_is_served_without_a_screen) {
  std::error_code ec;
  fs::remove_all(tmp(""), ec);
  int installs = 0;
  const Game g = demo(&installs);
  put(fs::path(tmp("copy")) / "demo.dat", "DEMO v2");
  put(fs::path(tmp("copy")) / "DEMO.EXE", "x");
  put(fs::path(tmp("other")) / "demo.dat", "NOPE");
  EXPECT(!rekit::sdl::serves_setup({}) && !rekit::sdl::serves_setup({"--look"}) && !rekit::sdl::serves_setup({"--else", "x"}));
  EXPECT(rekit::sdl::serves_setup({"--describe"}) && rekit::sdl::serves_setup({"--search"}) && rekit::sdl::serves_setup({"--install", "x"}));

  const Served d = ask(g, {"--describe"});
  EXPECT(d.r.exit == rekit::kSetupReady && d.last.ok && d.last.title == "Demo Game" && d.last.find_words.find("Demo Game needs your own copy") == 0 &&
         d.last.pick_words.find("DEMO.EXE") != std::string::npos && !d.last.find_sub.empty() && d.last.into == (fs::path(tmp("user")) / "install").string());
  const Served s = ask(g, {"--search"});
  EXPECT(s.r.exit == rekit::kSetupReady && s.last.ok && s.last.path == tmp("found"));

  const Served l = ask(g, {"--look", (fs::path(tmp("copy")) / "DEMO.EXE").string()});
  EXPECT(l.r.exit == rekit::kSetupReady && l.last.ok && !l.last.in_place && l.last.words == "An installed copy of Demo Game, version 2." &&
         l.last.into == (fs::path(tmp("user")) / "install").string() && l.r.install_dir.empty() && installs == 0);
  const Served no = ask(g, {"--look", tmp("other")});
  EXPECT(no.r.exit == rekit::kSetupFailed && !no.last.ok && no.last.summary.find("No copy of Demo Game was found in other.") == 0 && !no.r.error.empty());
  EXPECT(!ask(g, {"--install", tmp("other")}).last.ok && installs == 0 && !fs::exists(tmp("user"), ec));

  const Served i = ask(g, {"--install", tmp("copy")});
  EXPECT(i.r.exit == rekit::kSetupReady && i.last.ok && i.last.where == (fs::path(tmp("user")) / "install").string() && i.r.install_dir == i.last.where);
  EXPECT(i.last.summary == "Demo Game is installed." && i.other == 0 && installs == 1 && fs::exists(fs::path(i.last.where) / "demo.dat", ec));
  EXPECT(rekit::check_files(i.last.where, {}, nullptr).ok());  // the install's record, for CHECK MY FILES
  bool forward = !i.steps.empty();
  for (size_t k = 1; k < i.steps.size(); ++k) forward = forward && i.steps[k].fraction >= i.steps[k - 1].fraction;
  // The game's last stage, then the kit's record of the files (CHECK MY FILES), at the whole.
  EXPECT(forward && i.steps.size() > 2 && i.steps.back().stage == "Recording the files" && i.steps.back().fraction == 1.0);
  bool checked = false;
  for (const auto& st : i.steps) checked = checked || st.stage == "Checking";
  EXPECT(checked);
  // Looked at again: the game's own copy, in use where it is.
  const Served own = ask(g, {"--look", i.last.where});
  EXPECT(own.last.ok && own.last.in_place && own.last.where == i.last.where && own.r.install_dir == i.last.where && installs == 1);

  // Into a folder of the game's own; another's is refused and left as it was.
  const std::string mine = tmp("drive/Demo Game");
  const Served f = ask(g, {"--install", tmp("copy"), "--folder", mine}, tmp("user2"));
  EXPECT(f.last.ok && f.last.where == (fs::path(mine) / "install").make_preferred().string() && fs::exists(fs::path(mine) / rekit::kGameFolderMark, ec));
  put(fs::path(tmp("theirs")) / "letter.txt", "x");
  const Served t = ask(g, {"--install", tmp("copy"), "--folder", tmp("theirs")}, tmp("user3"));
  EXPECT(!t.last.ok && t.r.exit == rekit::kSetupFailed && !fs::exists(fs::path(tmp("theirs")) / "install", ec) && installs == 2);
}

#if !defined(_WIN32)  // stdin as a pipe
TEST(setup_served_stops_on_cancel) {
  std::error_code ec;
  fs::remove_all(tmp(""), ec);
  int installs = 0;
  Game g = demo(&installs);
  put(fs::path(tmp("copy")) / "demo.dat", "DEMO v2");
  auto inner = g.install;
  g.install = [inner](const rekit::Found& f, const std::string& out, const std::string& media, const rekit::sdl::ProgressFn& p) {
    for (int i = 0; i < 200; ++i) {  // long enough for the line to arrive
      if (!p({"Copying", 0.1, ""})) break;
      usleep(10000);
    }
    return inner(f, out, media, p);
  };
  int ends[2];
  EXPECT(pipe(ends) == 0);
  const int keep = dup(0);
  dup2(ends[0], 0);
  EXPECT(write(ends[1], "cancel\n", 7) == 7);
  const Served s = ask(g, {"--install", tmp("copy")});
  dup2(keep, 0);
  close(keep), close(ends[0]), close(ends[1]);
  EXPECT(s.r.exit == rekit::kSetupCancelled && s.last.cancelled && !s.last.ok && s.r.install_dir.empty() && installs == 0);
}
#endif

// A launcher's extras (2026-11-25): --copies' rows, --add and --first-run with their progress, each only for a game
// whose recipe sets the hook; a throw is a failure in its words.
TEST(setup_served_extras_when_the_game_has_them) {
  std::error_code ec;
  fs::remove_all(tmp(""), ec);
  int installs = 0;
  Game g = demo(&installs);
  for (const char* ask_for : {"--copies", "--add", "--first-run"}) {
    const std::vector<std::string> args = std::string(ask_for) == "--copies" ? std::vector<std::string>{ask_for}
                                                                             : std::vector<std::string>{ask_for, "x"};
    EXPECT(rekit::sdl::serves_setup(args));
    const Served none = ask(g, args);
    EXPECT(none.r.exit == rekit::kSetupFailed && !none.last.ok && none.last.summary == "not a setup this game answers" && none.steps.empty());
  }
  EXPECT(!rekit::sdl::serves_setup({"--add"}) && !rekit::sdl::serves_setup({"--first-run"}));
  std::string in_use_seen;
  g.copies = [&](const SetupOptions& o) {
    in_use_seen = o.in_use;
    return std::vector<rekit::sdl::CopyRow>{{"/games/a", "2", true, {}}, {"/games/b \"old\"", "", false, {"music", "movies"}}, {"", "1", false, {}}};
  };
  std::vector<std::string> got;
  g.add = [&](const std::string& pick, const SetupOptions&, const rekit::sdl::ProgressFn& p) {
    rekit::sdl::InstallResult r;
    for (int i = 0; i < 3; ++i)
      if (!p({"Adding the music", i / 3.0, "track0" + std::to_string(i + 2) + ".wav"})) return r.cancelled = true, r;
    got.push_back("add " + pick);
    r.ok = true, r.summary = "The music is added.";
    return r;
  };
  g.first_run = [&](const std::string& step, const SetupOptions&, const rekit::sdl::ProgressFn&) -> rekit::sdl::InstallResult {
    if (step != "movies") throw std::runtime_error("no first-run step " + step + ".");
    got.push_back("first " + step);
    rekit::sdl::InstallResult r;
    r.ok = true, r.summary = "The movies are converted.";
    return r;
  };
  const Served c = ask(g, {"--copies"});
  EXPECT(c.r.exit == rekit::kSetupReady && c.last.ok && c.steps.size() == 2 && c.other == 0);  // a row with no folder is not sent
  if (c.steps.size() == 2) {
    EXPECT(c.steps[0].copy == "/games/a" && c.steps[0].copy_data == "2" && c.steps[0].copy_in_use && c.steps[0].copy_missing.empty());
    EXPECT(c.steps[1].copy == "/games/b \"old\"" && c.steps[1].copy_data.empty() && !c.steps[1].copy_in_use &&
           c.steps[1].copy_missing == std::vector<std::string>({"music", "movies"}));
  }
  const Served a = ask(g, {"--add", "/discs/disc.cue"});
  EXPECT(a.r.exit == rekit::kSetupReady && a.last.ok && a.last.summary == "The music is added." && !a.steps.empty() &&
         a.steps[0].stage == "Adding the music" && a.steps[0].copy.empty());
  const Served f = ask(g, {"--first-run", "movies"});
  EXPECT(f.r.exit == rekit::kSetupReady && f.last.ok && f.last.summary == "The movies are converted.");
  const Served bad = ask(g, {"--first-run", "maps"});
  EXPECT(bad.r.exit == rekit::kSetupFailed && !bad.last.ok && bad.last.summary == "no first-run step maps." && bad.r.error == bad.last.summary);
  EXPECT(got == std::vector<std::string>({"add /discs/disc.cue", "first movies"}));
  // A row's message read by a launcher that knows none of this: a step with no stage (it shows nothing new).
  SetupMessage row;
  row.copy = "/x", row.copy_missing = {"music"};
  SetupMessage back;
  EXPECT(rekit::parse_setup_message(rekit::setup_message_json(row), &back) && !back.done && back.stage.empty() && back.copy == "/x");
}
