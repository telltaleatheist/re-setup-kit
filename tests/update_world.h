#pragma once
// A world on disk for the update checks (test_update_sdl.cpp, test_update_menu.cpp): file:/// stand-ins for a signed
// catalog, two homes and their releases, signed with test keys. SDL's copy of the environment set and put back.
#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "rekit/hash.h"
#include "rekit/sdl/update.h"
#include "test_keys.h"

namespace update_world {

namespace fs = std::filesystem;

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

inline void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
inline std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
inline std::string url(const fs::path& p) { return "file://" + p.generic_string(); }
inline std::string sha(const std::string& s) { return rekit::sha256_hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

// A world on disk: a catalog signed by key 1 with two homes keyed as the live catalog keys them ("om" the mirror first,
// then "gh" GitHub; folders mirror/ and github/), each with a releases list
// for repo "demo-re" holding v1.4.0 of `file_name` with `payload`, its SHA256SUMS signed by `sums_key`.
struct World {
  fs::path root;
  rekit::sdl::UpdateSource src;
  std::string file_name, payload;
  World(const std::string& name, const std::string& file_name_, const std::string& payload_, uint8_t mirror_sums_key = 1,
        uint8_t github_sums_key = 1, const std::string& notes = "New things.", const std::string& older_release = "")
      : root(fs::path(REKIT_TEST_TMP) / "update-sdl" / name), file_name(file_name_), payload(payload_) {
    fs::remove_all(root);
    const std::string cat = "{\"catalog\": 1, \"counts\": \"https://counts.example/count\", \"homes\": ["
                            "{\"key\": \"om\", \"releases\": \"" + url(root / "mirror" / "{repo}.json") + "\", \"order\": 1},"
                            "{\"key\": \"gh\", \"releases\": \"" + url(root / "github" / "{repo}.json") + "\", \"order\": 2}], \"games\": []}";
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
      put(root / home / "demo-re.json", "[{\"tag_name\": \"v1.4.0\", \"body\": \"" + notes + "\", \"html_url\": \"https://h.example/v1.4.0\", "
                                        "\"assets\": [" + assets + "]}" + older_release + "]");
    }
    src.recipe = {"demo", "demo-re", "v", "v1.2.0", file_name};
    src.catalogs = {url(root / "catalog.json")};
    src.keys = {test_keys::public_key(1)};
    src.temp = (root / "temp").string();
    src.allow_file = true, src.no_restart = true;
  }
};


}  // namespace update_world
