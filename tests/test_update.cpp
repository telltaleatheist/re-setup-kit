// The update engine's logic (rekit/update.h): the catalog's homes, a releases list judged for a game, the sums, the
// swap, the unpack, the Windows helper scripts, the choices file, the note and the count's address. No network.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/update.h"
#if REKIT_HAVE_LIBARCHIVE
#include "archive_maker.h"
#endif

namespace fs = std::filesystem;
using rekit::UpdateCheck;
using rekit::UpdateRecipe;

namespace {
void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
// A release in GitHub's JSON with these files (each at https://h.example/<tag>/<name>).
std::string release(const std::string& tag, const std::vector<std::string>& files, const std::string& notes = "",
                    bool draft = false, bool pre = false) {
  std::string assets;
  for (const std::string& f : files)
    assets += std::string(assets.empty() ? "" : ",") + "{\"name\": \"" + f + "\", \"size\": 12, \"browser_download_url\": \"https://h.example/" +
              tag + "/" + f + "\"}";
  return "{\"tag_name\": \"" + tag + "\", \"body\": \"" + notes + "\", \"html_url\": \"https://h.example/r/" + tag +
         "\", \"draft\": " + (draft ? "true" : "false") + ", \"prerelease\": " + (pre ? "true" : "false") + ", \"assets\": [" + assets + "]}";
}
std::vector<std::string> signed_files(const std::string& tag) {
  return {"Demo-" + tag + "-mac.dmg", "SHA256SUMS", "SHA256SUMS.sig"};
}
UpdateRecipe recipe(const std::string& tag = "v1.2.0") {
  UpdateRecipe r;
  r.key = "demo", r.repo = "demo-re", r.tag_prefix = "v", r.tag = tag, r.file = "Demo-{tag}-mac.dmg";
  return r;
}
}  // namespace

TEST(update_catalog_homes_and_counts) {
  const std::string cat = R"({"catalog": 1, "revision": 9, "counts": "https://site.example/count",
    "homes": [{"key": "github", "releases": "https://api.example/repos/o/{repo}/releases?per_page={count}", "order": 2},
              {"key": "mirror", "releases": "https://git.example/api/v1/repos/o/{repo}/releases?limit={count}", "order": 1},
              {"key": "plain", "releases": "http://bad.example/{repo}", "order": 0},
              {"key": "norepo", "releases": "https://x.example/releases", "order": 0},
              {"key": "local", "releases": "file:///tmp/{repo}.json", "order": 3}],
    "games": []})";
  rekit::UpdateCatalog c;
  std::string why;
  EXPECT(rekit::read_update_catalog(cat, false, &c, &why) && c.homes.size() == 2 && c.counts == "https://site.example/count");
  EXPECT(c.homes[0].key == "mirror" && c.homes[1].key == "github");  // by order
  EXPECT(rekit::releases_address(c.homes[1], "demo-re", 20) == "https://api.example/repos/o/demo-re/releases?per_page=20");
  EXPECT(rekit::read_update_catalog(cat, true, &c, &why) && c.homes.size() == 3 && c.homes[2].key == "local");  // checks only
  EXPECT(!rekit::read_update_catalog("{\"games\": []}", false, &c, &why) && why == "the catalog lists no homes");
  EXPECT(!rekit::read_update_catalog("not json", false, &c, &why));
}

TEST(update_release_tags_and_names) {
  EXPECT(rekit::release_tag("v", "v1.2.3") && rekit::release_tag("pm-v", "pm-v1.10.0"));
  EXPECT(!rekit::release_tag("v", "pm-v1.2.3") && !rekit::release_tag("v", "v1.2.3-rc1") && !rekit::release_tag("v", "v1.2"));
  EXPECT(!rekit::release_tag("v", "v1.2.3.4") && !rekit::release_tag("v", "v1..3") && !rekit::release_tag("v", "v"));
  EXPECT(rekit::release_file_name("MW3-alone-{tag}-mac.dmg", "v1.7.0") == "MW3-alone-v1.7.0-mac.dmg");
}

// The newest release with the recipe's tag and file across the homes, its sources (signed sums only) in the homes'
// order, and the notes of every newer release, newest first.
TEST(update_judges_the_homes) {
  const std::string github = "[" + release("v1.4.0", signed_files("v1.4.0"), "Four \\u2014 \\ud83d\\ude80") + "," +
                             release("v1.5.0", signed_files("v1.5.0"), "", true) + "," +          // a draft
                             release("v1.6.0", signed_files("v1.6.0"), "", false, true) + "," +   // a prerelease
                             release("pm-v9.0.0", {"Demo-pm-v9.0.0-mac.dmg"}) + "," +            // another game
                             release("v1.3.0", signed_files("v1.3.0"), "Three") + "," +
                             release("v1.1.0", signed_files("v1.1.0"), "One") + "]";
  const std::string mirror = "[" + release("v1.4.0", {"Demo-v1.4.0-mac.dmg", "SHA256SUMS"}) + "," +  // no signature
                             release("v1.3.5", signed_files("v1.3.5"), "Three and a half") + "]";
  UpdateCheck c = rekit::judge_update(recipe(), {{"mirror", mirror, ""}, {"github", github, ""}});
  EXPECT(c.kind == UpdateCheck::kNewer && c.tag == "v1.4.0" && c.page == "https://h.example/r/v1.4.0");
  EXPECT(c.sources.size() == 1 && c.sources[0].home == "github" && c.sources[0].file.name == "Demo-v1.4.0-mac.dmg" &&
         c.sources[0].sig.url == "https://h.example/v1.4.0/SHA256SUMS.sig");
  EXPECT(c.notes.size() == 3 && c.notes[0].first == "v1.4.0" && c.notes[1].first == "v1.3.5" && c.notes[2].first == "v1.3.0");
  EXPECT(c.notes[0].second == "Four \xE2\x80\x94 \xF0\x9F\x9A\x80");  // \u escapes and a surrogate pair as UTF-8
  EXPECT(c.words == "Version 1.4.0 is available (this copy is 1.2.0).");
  // A build after a release counts as that release; the newest is up to date.
  c = rekit::judge_update(recipe("v1.4.0-3-gabc-dirty"), {{"github", github, ""}});
  EXPECT(c.kind == UpdateCheck::kUpToDate && c.notes.empty());
  // Only an unsigned copy anywhere: newer, but nothing to install from.
  c = rekit::judge_update(recipe(), {{"mirror", mirror, ""}});
  EXPECT(c.kind == UpdateCheck::kNewer && c.tag == "v1.4.0" && c.sources.empty() && c.words.find("no signed checksums") != std::string::npos);
  // One home failing is no failure; all failing is, with the first reason.
  c = rekit::judge_update(recipe(), {{"mirror", "", "timed out"}, {"github", github, ""}});
  EXPECT(c.kind == UpdateCheck::kNewer);
  c = rekit::judge_update(recipe(), {{"mirror", "", "timed out"}, {"github", "<html>", ""}});
  EXPECT(c.kind == UpdateCheck::kFailed && c.words == "No update could be checked for: timed out.");
  // This copy's version unknown, or another game's tag: no verdict.
  EXPECT(rekit::judge_update(recipe(""), {{"github", github, ""}}).kind == UpdateCheck::kFailed);
  EXPECT(rekit::judge_update(recipe("pm-v1.0.0"), {{"github", github, ""}}).kind == UpdateCheck::kFailed);
  // A list with no release of the game.
  EXPECT(rekit::judge_update(recipe(), {{"github", "[]", ""}}).kind == UpdateCheck::kUpToDate);
}

TEST(update_sums_line) {
  const std::string h(64, 'A'), g(64, 'b');
  const std::string sums = h + "  Demo-v1.4.0-mac.dmg\r\n" + g + " *Demo-v1.4.0-win64.zip\nnot a line\n";
  EXPECT(rekit::sums_line(sums, "Demo-v1.4.0-mac.dmg") == std::string(64, 'a'));
  EXPECT(rekit::sums_line(sums, "Demo-v1.4.0-win64.zip") == g && rekit::sums_line(sums, "Demo").empty());
}

// The swap: a file and a folder replaced; an earlier try's leftovers cleared when they are the game's, and a
// stranger's left alone (the swap refused, nothing changed).
TEST(update_swap_in) {
  const fs::path dir = fs::path(REKIT_TEST_TMP) / "update-swap";
  std::string error;
  put(dir / "Demo.AppImage", "old"), put(dir / "fresh.AppImage", "new");
  put(dir / "Demo.AppImage.old", "an earlier try's");
  EXPECT(rekit::swap_in((dir / "fresh.AppImage").string(), (dir / "Demo.AppImage").string(), "", &error));
  EXPECT(read(dir / "Demo.AppImage") == "new" && !fs::exists(dir / "Demo.AppImage.old") && !fs::exists(dir / "Demo.AppImage.new"));
  put(dir / "Game" / "demo.exe", "old"), put(dir / "Game" / "data" / "a.txt", "a");
  put(dir / "fresh" / "demo.exe", "new"), put(dir / "fresh" / "data" / "b.txt", "b");
  put(dir / "Game.new" / "demo.exe", "a broken earlier try");
  EXPECT(rekit::swap_in((dir / "fresh").string(), (dir / "Game").string(), "demo.exe", &error));
  EXPECT(read(dir / "Game" / "demo.exe") == "new" && fs::exists(dir / "Game" / "data" / "b.txt") && !fs::exists(dir / "Game" / "data" / "a.txt"));
  put(dir / "Game.old" / "photo.jpg", "the player's");
  EXPECT(!rekit::swap_in((dir / "fresh").string(), (dir / "Game").string(), "demo.exe", &error) && error.find("is not this game's") != std::string::npos);
  EXPECT(read(dir / "Game.old" / "photo.jpg") == "the player's" && read(dir / "Game" / "demo.exe") == "new");
  EXPECT(!rekit::swap_in((dir / "gone").string(), (dir / "Game").string(), "demo.exe", &error));
}

#if REKIT_HAVE_LIBARCHIVE
TEST(update_unpacks_a_release) {
  const fs::path dir = fs::path(REKIT_TEST_TMP) / "update-unpack";
  fs::create_directories(dir);
  std::string error;
  put(dir / "one-top.zip", write_archive("zip", {{"Demo-v1.4.0/demo.exe", "new"}, {"Demo-v1.4.0/data/a.txt", "a"}}));
  EXPECT(rekit::unpack_release((dir / "one-top.zip").string(), (dir / "out1").string(), &error));
  EXPECT(read(dir / "out1" / "demo.exe") == "new" && read(dir / "out1" / "data" / "a.txt") == "a");
  put(dir / "flat.zip", write_archive("zip", {{"demo.exe", "new"}, {"readme.txt", "r"}}));
  EXPECT(rekit::unpack_release((dir / "flat.zip").string(), (dir / "out2").string(), &error) && read(dir / "out2" / "readme.txt") == "r");
  EXPECT(!rekit::unpack_release((dir / "flat.zip").string(), (dir / "out2").string(), &error));  // in the way
  put(dir / "evil.zip", write_archive("zip", {{"../escape.txt", "x"}}));
  EXPECT(!rekit::unpack_release((dir / "evil.zip").string(), (dir / "out3").string(), &error) && !fs::exists(dir / "out3") &&
         !fs::exists(dir / "escape.txt"));
  put(dir / "junk.zip", "not an archive");
  EXPECT(!rekit::unpack_release((dir / "junk.zip").string(), (dir / "out4").string(), &error) && !fs::exists(dir / "out4"));
}
#endif

TEST(update_windows_scripts) {
  const std::string s = rekit::windows_setup_script("C:\\Temp\\Demo-Setup-v1.4.0.exe", false, 4321, "C:\\Games\\Demo\\demo.exe");
  EXPECT(s.find("tasklist /FI \"PID eq 4321\" /NH 2>nul | find \" 4321 \"") != std::string::npos);
  EXPECT(s.find("start \"\" /wait \"C:\\Temp\\Demo-Setup-v1.4.0.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER\r\n") != std::string::npos);
  EXPECT(s.find("start \"\" \"C:\\Games\\Demo\\demo.exe\"\r\n") != std::string::npos && s.find("timeout") == std::string::npos);
  EXPECT(rekit::windows_setup_script("C:\\s.exe", true, 1, "").find("/ALLUSERS") != std::string::npos);
  const std::string f = rekit::windows_folder_script("C:\\Games\\Demo.new", "C:\\Games\\Demo", 7, "C:\\Games\\Demo\\demo.exe");
  EXPECT(f.find("move \"C:\\Games\\Demo\" \"C:\\Games\\Demo.old\" >nul || goto keep") != std::string::npos);
  EXPECT(f.find("|| (move \"C:\\Games\\Demo.old\" \"C:\\Games\\Demo\" >nul & goto keep)") != std::string::npos);
  EXPECT(rekit::windows_setup_script("C:\\100%\\s.exe", false, 1, "").empty() && rekit::windows_folder_script("a\"b", "c", 1, "").empty());
}

TEST(update_choices_note_and_count) {
  const fs::path user = fs::path(REKIT_TEST_TMP) / "update-user";
  rekit::UpdatePrefs p = rekit::read_update_prefs(user.string());
  EXPECT(p.ask_at_start && p.last_check == 0 && rekit::start_check_due(p, 1764000000));
  p.ask_at_start = false, p.last_check = 5000;
  EXPECT(rekit::write_update_prefs(user.string(), p));
  p = rekit::read_update_prefs(user.string());
  EXPECT(!p.ask_at_start && p.last_check == 5000 && !rekit::start_check_due(p, 999999));  // unticked: never
  p.ask_at_start = true;
  EXPECT(!rekit::start_check_due(p, 5000 + 3600) && rekit::start_check_due(p, 5000 + 86400) && rekit::start_check_due(p, 4000));
  std::string from, to;
  bool github = false;
  EXPECT(!rekit::take_update_note(user.string(), &from, &to, &github));
  EXPECT(rekit::write_update_note(user.string(), "1.2.0", "1.4.0", true));
  EXPECT(rekit::take_update_note(user.string(), &from, &to, &github) && from == "1.2.0" && to == "1.4.0" && github);
  EXPECT(!rekit::take_update_note(user.string(), &from, &to, &github));  // taken once
  EXPECT(rekit::update_count_url("https://s.example/c", "demo", "1.2.0", "1.4.0", false, "game") ==
         "https://s.example/c?kind=update&game=demo&from=1.2.0&to=1.4.0&launcher=game&src=mirror");
  EXPECT(rekit::update_count_url("https://s.example/c?x=1", "de mo", "", "1.4", true, "game") ==
         "https://s.example/c?x=1&kind=update&game=de_mo&from=unknown&to=1.4&launcher=game&src=github");
}
