// The update engine's SDL half (rekit/sdl/update.h) against file:/// stand-ins for the catalog, the homes and the
// release files, signed with test keys: the check, the download (signed sums only), the install's backup, note and swap
// (a folder and an AppImage; no restart), the finished update's count and the start-up check's rules.
#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/hash.h"
#include "rekit/sdl/update.h"
#include "test_keys.h"
#if REKIT_HAVE_LIBARCHIVE
#include "archive_maker.h"
#endif

namespace fs = std::filesystem;
using rekit::UpdateCheck;
using rekit::sdl::UpdateSource;

namespace {

struct Env {  // SDL's copy of the environment (the kit reads it there), put back as it was after
  std::vector<std::pair<std::string, std::string>> was;
  std::vector<std::string> unset;
  void set(const char* name, const char* value) {
    if (const char* v = SDL_getenv(name)) was.push_back({name, v});
    else unset.push_back(name);
    if (value) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
    else SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
  }
  ~Env() {
    for (const auto& [n, v] : was) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), n.c_str(), v.c_str(), true);
    for (const std::string& n : unset) SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), n.c_str());
  }
};

void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
std::string url(const fs::path& p) { return "file://" + p.generic_string(); }
std::string sha(const std::string& s) { return rekit::sha256_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

// A world on disk: a catalog signed by key 1 with two homes ("mirror" first, then "github"), each with a releases list
// for repo "demo-re" holding v1.4.0 of `file_name` with `payload`, its SHA256SUMS signed by `sums_key`.
struct World {
  fs::path root;
  UpdateSource src;
  std::string file_name, payload;
  World(const std::string& name, const std::string& file_name_, const std::string& payload_, uint8_t mirror_sums_key = 1,
        uint8_t github_sums_key = 1)
      : root(fs::path(REKIT_TEST_TMP) / "update-sdl" / name), file_name(file_name_), payload(payload_) {
    fs::remove_all(root);
    const std::string cat = "{\"catalog\": 1, \"counts\": \"https://counts.example/count\", \"homes\": ["
                            "{\"key\": \"mirror\", \"releases\": \"" + url(root / "mirror" / "{repo}.json") + "\", \"order\": 1},"
                            "{\"key\": \"github\", \"releases\": \"" + url(root / "github" / "{repo}.json") + "\", \"order\": 2}], \"games\": []}";
    put(root / "catalog.json", cat), put(root / "catalog.json.sig", test_keys::sign(cat, 1));
    for (const auto& [home, key] : {std::pair<std::string, uint8_t>{"mirror", mirror_sums_key}, {"github", github_sums_key}}) {
      const fs::path d = root / home / "v1.4.0";
      const std::string named = rekit::release_file_name(file_name, "v1.4.0");
      const std::string sums = sha(payload) + "  " + named + "\n";
      put(d / named, payload), put(d / "SHA256SUMS", sums), put(d / "SHA256SUMS.sig", test_keys::sign(sums, key));
      std::string assets;
      for (const char* f : {named.c_str(), "SHA256SUMS", "SHA256SUMS.sig"})
        assets += std::string(assets.empty() ? "" : ",") + "{\"name\": \"" + f + "\", \"size\": " + std::to_string(fs::file_size(d / f)) +
                  ", \"browser_download_url\": \"" + url(d / f) + "\"}";
      put(root / home / "demo-re.json", "[{\"tag_name\": \"v1.4.0\", \"body\": \"New things.\", \"html_url\": \"https://h.example/v1.4.0\", "
                                        "\"assets\": [" + assets + "]}]");
    }
    src.recipe = {"demo", "demo-re", "v", "v1.2.0", file_name};
    src.catalogs = {url(root / "catalog.json")};
    src.keys = {test_keys::public_key(1)};
    src.temp = (root / "temp").string();
    src.allow_file = true, src.no_restart = true;
  }
};

}  // namespace

TEST(update_sdl_check_and_download) {
  Env env;
  env.set("RE_KIT_TEST", nullptr);
  World w("check", "Demo-{tag}.AppImage", "the new program");
  std::string counts;
  UpdateCheck c = rekit::sdl::check_update(w.src, &counts, nullptr);
  EXPECT(c.kind == UpdateCheck::kNewer && c.tag == "v1.4.0" && c.sources.size() == 2 && counts == "https://counts.example/count");
  EXPECT(c.notes.size() == 1 && c.notes[0].second == "New things.");
  std::string error;
  const std::string dir = rekit::sdl::update_dir(w.src.temp, "demo", &error);
  std::string file;
  bool github = true, cancelled = false;
  EXPECT(rekit::sdl::download_update(w.src, c, dir, nullptr, &file, &github, &error, &cancelled));
  EXPECT(read(file) == "the new program" && !github && fs::path(file).filename() == "Demo-v1.4.0.AppImage");
  // A catalog signed by another key: no check at all.
  UpdateSource other = w.src;
  other.keys = {test_keys::public_key(2)};
  c = rekit::sdl::check_update(other, &counts, nullptr);
  EXPECT(c.kind == UpdateCheck::kFailed && c.words.find("signature does not check") != std::string::npos && counts.empty());
  // A test run asks nothing over the network (file:/// stand-ins are not the network).
  env.set("RE_KIT_TEST", "1");
  UpdateSource net = w.src;
  net.catalogs = {"https://catalog.example/games.json"};
  EXPECT(rekit::sdl::check_update(net, &counts, nullptr).words.find("no network in a test run") != std::string::npos);
}

// The mirror's sums signed by a stranger, then the GitHub file swapped for another: each source judged on its own.
TEST(update_sdl_download_needs_signed_sums) {
  Env env;
  env.set("RE_KIT_TEST", nullptr);
  World w("signed", "Demo-{tag}.AppImage", "the new program", 2, 1);
  std::string counts, error, file;
  const UpdateCheck c = rekit::sdl::check_update(w.src, &counts, nullptr);
  const std::string dir = rekit::sdl::update_dir(w.src.temp, "demo", &error);
  bool github = false, cancelled = false;
  EXPECT(rekit::sdl::download_update(w.src, c, dir, nullptr, &file, &github, &error, &cancelled) && github);
  put(w.root / "github" / "v1.4.0" / "Demo-v1.4.0.AppImage", "a different pgm");  // same size, wrong sum
  EXPECT(!rekit::sdl::download_update(w.src, c, dir, nullptr, &file, &github, &error, &cancelled));
  EXPECT(error.find("mirror: its checksums' signature does not check") != std::string::npos &&
         error.find("github: it does not match its checksum") != std::string::npos);
  EXPECT(!fs::exists(fs::path(dir) / "Demo-v1.4.0.AppImage"));
}

#if !defined(_WIN32)
// An AppImage swapped in place with its execute bit, the player's files backed up first, the note left for the next
// start; that start sends the count once (the address curl was given), and not for a version the update did not reach.
TEST(update_sdl_install_appimage_and_count) {
  Env env;
  env.set("RE_KIT_TEST", nullptr);
  World w("appimage", "Demo-{tag}.AppImage", "the new program");
  const fs::path user = w.root / "user", app = w.root / "apps" / "Demo.AppImage";
  put(app, "the old program"), put(user / "saves" / "one.sav", "progress");
  w.src.target = app.string(), w.src.keep = {(user / "saves").string()};
  std::string counts, error, file;
  bool github = false, cancelled = false;
  const UpdateCheck c = rekit::sdl::check_update(w.src, &counts, nullptr);
  const std::string dir = rekit::sdl::update_dir(w.src.temp, "demo", &error);
  EXPECT(rekit::sdl::download_update(w.src, c, dir, nullptr, &file, &github, &error, &cancelled));
  const rekit::sdl::UpdateInstall r = rekit::sdl::install_update(w.src, rekit::InstallKind::kAppImage, user.string(), file, github, c.tag, counts);
  EXPECT(r.quit && read(app) == "the new program" && (fs::status(app).permissions() & fs::perms::owner_exec) != fs::perms::none);
  EXPECT(!r.backup.empty() && read(fs::path(r.backup) / "saves" / "one.sav") == "progress" && !fs::exists(file));
  EXPECT(fs::exists(user / rekit::kUpdateNoteName));
  const fs::path args = w.root / "curl-args.txt";
  env.set("REKIT_CURL_ARGS_TO", args.string().c_str());
  UpdateSource before = w.src;  // the old program starting again: the update did not take, nothing is counted
  rekit::sdl::report_finished_update(before, user.string());
  EXPECT(!fs::exists(args) && !fs::exists(user / rekit::kUpdateNoteName));
  rekit::write_update_note(user.string(), {"1.2.0", "1.4.0", counts, false});
  UpdateSource after = w.src;
  after.recipe.tag = "v1.4.0";
  rekit::sdl::report_finished_update(after, user.string());
  EXPECT(read(args).find("https://counts.example/count?kind=update&game=demo&from=1.2.0&to=1.4.0&launcher=game&src=mirror\n") != std::string::npos);
  rekit::sdl::report_finished_update(after, user.string());  // once
  EXPECT(read(args).find("kind=update") == read(args).rfind("kind=update"));
  // Nothing changes for a development build or an unknown install.
  EXPECT(!rekit::sdl::install_update(w.src, rekit::InstallKind::kDevelopment, user.string(), file, false, c.tag, counts).quit);
}

#if REKIT_HAVE_LIBARCHIVE
TEST(update_sdl_install_folder) {
  Env env;
  env.set("RE_KIT_TEST", nullptr);
  World w("folder", "Demo-{tag}-linux.zip",
          write_archive("zip", {{"Demo-v1.4.0/demo", "new"}, {"Demo-v1.4.0/data/b.txt", "b"}}));
  const fs::path user = w.root / "user", game = w.root / "games" / "Demo";
  put(game / "demo", "old"), put(game / "data" / "a.txt", "a");
  w.src.target = game.string(), w.src.marker = "demo";
  std::string counts, error, file;
  bool github = false, cancelled = false;
  const UpdateCheck c = rekit::sdl::check_update(w.src, &counts, nullptr);
  const std::string dir = rekit::sdl::update_dir(w.src.temp, "demo", &error);
  EXPECT(rekit::sdl::download_update(w.src, c, dir, nullptr, &file, &github, &error, &cancelled));
  // A stranger's folder where the old copy goes aside: nothing is changed, and no note is left.
  put(w.root / "games" / "Demo.old" / "photo.jpg", "the player's");
  rekit::sdl::UpdateInstall r = rekit::sdl::install_update(w.src, rekit::InstallKind::kFolder, user.string(), file, github, c.tag, counts);
  EXPECT(!r.quit && r.words.find("is not this game's") != std::string::npos && read(game / "demo") == "old");
  EXPECT(!fs::exists(user / rekit::kUpdateNoteName));
  fs::remove_all(w.root / "games" / "Demo.old");
  EXPECT(rekit::sdl::download_update(w.src, c, dir, nullptr, &file, &github, &error, &cancelled));
  r = rekit::sdl::install_update(w.src, rekit::InstallKind::kFolder, user.string(), file, github, c.tag, counts);
  EXPECT(r.quit && read(game / "demo") == "new" && read(game / "data" / "b.txt") == "b" && !fs::exists(game / "data" / "a.txt"));
  EXPECT(r.backup.empty() && !fs::exists(fs::path(dir) / "unpacked"));
}
#endif
#endif

// The start-up check: once a day, never when the player unticked it, the launcher started the game or in a test run.
TEST(update_sdl_start_check_rules) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", nullptr);
  World w("start", "Demo-{tag}.AppImage", "the new program");
  const fs::path user = w.root / "user";
  int64_t now = 1764000000;
  w.src.now_s = [&now]() { return now; };
  const auto run = [&](UpdateCheck* c) {
    rekit::sdl::StartUpdateCheck s(w.src, user.string());
    std::string counts;
    for (int i = 0; i < 400 && s.running(); ++i) SDL_Delay(25);
    return s.take(c, &counts) && !s.take(c, &counts);
  };
  UpdateCheck c;
  EXPECT(run(&c) && c.kind == UpdateCheck::kNewer && rekit::read_update_prefs(user.string()).last_check == now);
  now += 3600;  // within the day: not asked
  EXPECT(run(&c) && c.kind == UpdateCheck::kFailed && c.words.empty());
  now += 86400;
  env.set("RE_LAUNCHER", "2.2.0");
  EXPECT(run(&c) && c.words.empty());
  env.set("RE_LAUNCHER", nullptr), env.set("RE_KIT_TEST", "1");
  EXPECT(run(&c) && c.words.empty());
  env.set("RE_KIT_TEST", nullptr);
  rekit::write_update_prefs(user.string(), {false, 0});  // DON'T ASK AGAIN
  EXPECT(run(&c) && c.words.empty());
  rekit::write_update_prefs(user.string(), {true, 0});
  EXPECT(run(&c) && c.kind == UpdateCheck::kNewer);
}
