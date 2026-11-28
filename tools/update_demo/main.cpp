// A demo game for real-system runs of the update engine (rekit/sdl/update.h), with no window: built twice, as v1.2.0 and
// v1.4.0 (REKIT_DEMO_TAG), it updates itself from a world of file:/// stand-ins signed with the test keys (never a real
// key) and starts again. Each start appends a line to <user>/demo-log.txt and reports a finished update's count; curl's
// arguments go to <user>/curl-args.txt (the count's address, which never resolves: counts.invalid).
//
//   rekit_update_demo world <root> <release file>   the catalog, its home and the release (the file's name with the
//                                                    tag as {tag} is the recipe's), all signed
//   rekit_update_demo run [--update]                 the game; its settings from the environment (a restart from a
//                                                    Windows helper keeps it, and starts it with no arguments) or these
//                                                    flags (a Mac app's `open --args`):
//       --user <dir> --world <root> --kind <windows-setup-user|windows-setup-machine|folder|appimage|mac-app>
//       --target <what is replaced> --marker <path inside a folder> --file <the release file pattern>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rekit/hash.h"
#include "rekit/program_record.h"
#include "rekit/sdl/update.h"
#include "test_keys.h"

#ifndef REKIT_DEMO_TAG
#define REKIT_DEMO_TAG "v1.2.0"
#endif

namespace fs = std::filesystem;

namespace {

std::string url(const fs::path& p) {
  const std::u8string s = fs::absolute(p).generic_u8string();
  std::string out(s.begin(), s.end());
  return out.front() == '/' ? "file://" + out : "file:///" + out;
}
void put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << s;
}

int world(const fs::path& root, const fs::path& release) {
  const std::string name = release.filename().string(), tag = "v1.4.0";
  if (name.find(tag) == std::string::npos) return std::fprintf(stderr, "the release file's name needs %s\n", tag.c_str()), 2;
  const fs::path rel = root / "github" / tag;
  fs::create_directories(rel);
  fs::copy_file(release, rel / name, fs::copy_options::overwrite_existing);
  const std::string sums = rekit::sha256_file_hex((rel / name).string()) + "  " + name + "\n";
  put(rel / "SHA256SUMS", sums), put(rel / "SHA256SUMS.sig", test_keys::sign(sums, 1));
  std::string assets;
  for (const std::string& f : {name, std::string("SHA256SUMS"), std::string("SHA256SUMS.sig")})
    assets += std::string(assets.empty() ? "" : ",") + "{\"name\": \"" + f + "\", \"size\": " + std::to_string(fs::file_size(rel / f)) +
              ", \"browser_download_url\": \"" + url(rel / f) + "\"}";
  put(root / "github" / "demo-re.json", "[{\"tag_name\": \"" + tag + "\", \"body\": \"- The demo's next version.\", \"html_url\": "
                                        "\"https://h.invalid/" + tag + "\", \"assets\": [" + assets + "]}]");
  const std::string cat = "{\"catalog\": 1, \"counts\": \"https://counts.invalid/count\", \"homes\": [{\"key\": \"github\", "
                          "\"releases\": \"" + url(root / "github" / "{repo}.json") + "\", \"order\": 1}], \"games\": []}";
  put(root / "catalog.json", cat), put(root / "catalog.json.sig", test_keys::sign(cat, 1));
  std::printf("world: %s (release %s, %s)\n", root.string().c_str(), name.c_str(), test_keys::public_key(1).c_str());
  return 0;
}

rekit::InstallKind kind_of(const std::string& k) {
  using K = rekit::InstallKind;
  return k == "windows-setup-user" ? K::kWindowsSetupUser : k == "windows-setup-machine" ? K::kWindowsSetupMachine
       : k == "folder" ? K::kFolder : k == "appimage" ? K::kAppImage : k == "mac-app" ? K::kMacApp : K::kUnknown;
}

int run(int argc, char** argv) {
  std::string user, root, kind, target, marker, file;
  bool update = false;
  const auto env = [](const char* n) { const char* v = SDL_getenv(n); return std::string(v ? v : ""); };
  user = env("REKIT_DEMO_USER"), root = env("REKIT_DEMO_WORLD"), kind = env("REKIT_DEMO_KIND"),
  target = env("REKIT_DEMO_TARGET"), marker = env("REKIT_DEMO_MARKER"), file = env("REKIT_DEMO_FILE");
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    const std::string v = i + 1 < argc ? argv[i + 1] : "";
    if (a == "--update") update = true;
    else if (a == "--user") user = v, ++i;
    else if (a == "--world") root = v, ++i;
    else if (a == "--kind") kind = v, ++i;
    else if (a == "--target") target = v, ++i;
    else if (a == "--marker") marker = v, ++i;
    else if (a == "--file") file = v, ++i;
  }
  if (user.empty() || root.empty()) return std::fprintf(stderr, "run: no --user or --world\n"), 2;
  fs::create_directories(user);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "REKIT_CURL_ARGS_TO", (fs::path(user) / "curl-args.txt").string().c_str(), true);
  std::ofstream log(fs::path(user) / "demo-log.txt", std::ios::app);
  const auto say = [&](const std::string& s) {
    log << s << "\n", log.flush();
    std::printf("%s\n", s.c_str()), std::fflush(stdout);
  };
  say("started " + std::string(REKIT_DEMO_TAG) + " as " + rekit::self_path() + " at " + std::to_string(std::time(nullptr)));
  rekit::sdl::UpdateSource src;
  src.recipe = {"demo", "demo-re", "v", REKIT_DEMO_TAG, file};
  src.catalogs = {url(fs::path(root) / "catalog.json")};
  src.keys = {test_keys::public_key(1)};
  src.target = target, src.marker = marker;
  src.keep = {(fs::path(user) / "saves").string()};
  src.allow_file = true;
  src.restart_args = {"run", "--user", user, "--world", root, "--kind", kind, "--target", target, "--file", file};
  if (!marker.empty()) src.restart_args.insert(src.restart_args.end(), {"--marker", marker});
  if (const std::string u = rekit::sdl::report_finished_update(src, user); !u.empty()) say("the update to " + u + " did not finish");
  if (!update) return say("done " + std::string(REKIT_DEMO_TAG)), 0;
  std::string counts;
  const rekit::UpdateCheck c = rekit::sdl::check_update(src, &counts, nullptr);
  say("check: " + c.words);
  if (c.kind != rekit::UpdateCheck::kNewer) return 1;
  const rekit::InstallKind k = kind_of(kind);
  if (const std::string why = rekit::sdl::update_blocked(src, k); !why.empty()) return say("blocked: " + why), 1;
  std::string error, got;
  bool github = false, cancelled = false;
  const std::string dir = rekit::sdl::update_dir("", "demo", &error);
  if (dir.empty() || !rekit::sdl::download_update(src, c, dir, nullptr, &got, &github, &error, &cancelled))
    return say("download failed: " + error), 1;
  say("downloaded " + got);
  const rekit::sdl::UpdateInstall r = rekit::sdl::install_update(src, k, user, got, github, c.tag, counts);
  say(std::string("install: ") + (r.quit ? "quit" : "stay") + ": " + r.words + (r.backup.empty() ? "" : " (backup " + r.backup + ")"));
  return r.quit ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 4 && std::strcmp(argv[1], "world") == 0) return world(argv[2], argv[3]);
  if (argc >= 2 && std::strcmp(argv[1], "run") == 0) return run(argc, argv);
  if (argc == 1 && SDL_getenv("REKIT_DEMO_USER")) return run(argc, argv);  // restarted by a Windows helper: no arguments
  std::fprintf(stderr, "rekit_update_demo %s: world <root> <release file> | run [--update] ...\n", REKIT_DEMO_TAG);
  return 2;
}
