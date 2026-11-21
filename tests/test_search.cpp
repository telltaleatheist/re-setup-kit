// The install search (rekit/search.h) on Linux and the Mac: Wine and bottle
// prefixes under a made-up home, the home folder's own, the program's folder.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/search.h"

namespace fs = std::filesystem;

TEST(search_reads_installer_values) {
#if defined(_WIN32)
  EXPECT(rekit::install_of_program_value("\"C:\\Games\\MW3\\Mech3.exe\"") == "C:\\Games\\MW3");
#endif
  EXPECT(rekit::install_of_program_value(" /opt/mw3/ ") == " /opt/mw3");
  EXPECT(rekit::install_of_program_value("/opt/mw3/MECH3.EXE") == "/opt/mw3");
}

#if !defined(_WIN32)
TEST(search_finds_prefixes_bottles_and_the_program_folder) {
  const std::string home = std::string(REKIT_TEST_TMP) + "/home";
  fs::create_directories(home + "/.wine/drive_c/Program Files (x86)/Pub/Game");
  fs::create_directories(home + "/.wine/drive_c/Games/Game");
  fs::create_directories(home + "/.wine/drive_c/Program Files/Games/Game");  // a hand-picked name: drive_c only
  fs::create_directories(home + "/Library/Application Support/CrossOver/Bottles/b1/drive_c/Pub/Game");
  const char* old_home = std::getenv("HOME");
  const std::string saved = old_home ? old_home : "";
  const char* old_wp = std::getenv("WINEPREFIX");
  const std::string saved_wp = old_wp ? old_wp : "";
  setenv("HOME", home.c_str(), 1);
  unsetenv("WINEPREFIX");
  rekit::SearchHints h;
  h.installer_folders = {"Pub/Game"};
  h.other_folders = {"Games/Game"};
  h.drop_in = false;
  const std::vector<std::string> c = rekit::install_candidates(h, "/apps/Game.app/Contents/Resources/");
  if (old_home) setenv("HOME", saved.c_str(), 1);
  if (old_wp) setenv("WINEPREFIX", saved_wp.c_str(), 1);
  const std::vector<std::string> want = {home + "/.wine/drive_c/Program Files (x86)/Pub/Game",
                                         home + "/.wine/drive_c/Games/Game",
                                         home + "/Library/Application Support/CrossOver/Bottles/b1/drive_c/Pub/Game",
                                         home + "/Games/Game",
                                         "/apps/Game.app/Contents/Resources",
                                         "/apps/Game.app/Contents",
                                         "/apps"};
  EXPECT(c == want);
  for (const std::string& s : c) std::printf("  %s\n", s.c_str());
}
#endif

#if !defined(_WIN32)
TEST(search_finds_a_game_folder_dropped_beside_the_program_or_its_appimage) {
  const std::string root = std::string(REKIT_TEST_TMP) + "/drop";
  fs::create_directories(root + "/bin");
  fs::create_directories(root + "/My Old MW3 Copy");
  fs::create_directories(root + "/.hidden");
  fs::create_directories(root + "/images/Game Files");
  const char* old_image = std::getenv("APPIMAGE");
  const std::string saved = old_image ? old_image : "";
  setenv("APPIMAGE", (root + "/images/Game.AppImage").c_str(), 1);
  rekit::SearchHints h;  // no folders named: only the program's places
  const std::vector<std::string> c = rekit::install_candidates(h, root + "/bin/");
  if (old_image) setenv("APPIMAGE", saved.c_str(), 1);
  else unsetenv("APPIMAGE");
  auto has = [&](const std::string& d) { return std::find(c.begin(), c.end(), d) != c.end(); };
  for (const std::string& s : c) std::printf("  %s\n", s.c_str());
  EXPECT(has(root + "/bin") && has(root) && has(root + "/My Old MW3 Copy") && has(root + "/images") &&
         has(root + "/images/Game Files"));
  EXPECT(!has(root + "/.hidden"));
  h.drop_in = false;
  const std::vector<std::string> d = rekit::install_candidates(h, root + "/bin/");
  EXPECT(std::find(d.begin(), d.end(), root + "/My Old MW3 Copy") == d.end());
}
#endif
