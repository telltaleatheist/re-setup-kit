// Safe paths, free space and the swap (rekit/files.h).
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "rekit/files.h"

namespace fs = std::filesystem;

TEST(files_safe_relative_refuses_paths_that_leave) {
  using rekit::safe_relative;
  EXPECT(safe_relative("MechWarrior 3/zbd/reader.zbd") == "MechWarrior 3/zbd/reader.zbd");
  EXPECT(safe_relative("a\\b\\c.txt") == "a/b/c.txt" && safe_relative("./a//b") == "a/b");
  EXPECT(safe_relative("/etc/passwd") == "" && safe_relative("\\\\server\\share\\x") == "");
  EXPECT(safe_relative("C:/Windows/x") == "" && safe_relative("c:x") == "");
  EXPECT(safe_relative("../x") == "" && safe_relative("..\\x") == "" && safe_relative("a/../../x") == "");
  EXPECT(safe_relative("a/..") == "" && safe_relative("") == "" && safe_relative("./") == "");
  const std::string base = std::string(REKIT_TEST_TMP) + "/inside";
  fs::create_directories(base);
  EXPECT(rekit::path_inside(base, base + "/a/b") && !rekit::path_inside(base, base) && !rekit::path_inside(base, base + "/../x"));
}

TEST(files_has_room_says_how_much) {
  const std::string dir = REKIT_TEST_TMP;
  EXPECT(rekit::has_room(dir, 1024));
  const uint64_t keep = rekit::space_spare();
  rekit::space_spare() = uint64_t{1} << 60;
  uint64_t needed = 0, free = 0;
  EXPECT(!rekit::has_room(dir, 1024, &needed, &free) && needed == (uint64_t{1} << 60) + 1024 && free < needed);
  EXPECT(rekit::has_room(dir, -1));  // unknown size
  rekit::space_spare() = keep;
}

TEST(files_swap_replaces_only_our_own_folder) {
  const fs::path t = fs::path(REKIT_TEST_TMP) / "swap";
  auto make = [](const fs::path& d, const char* file, const char* text) {
    fs::create_directories(d);
    std::ofstream(d / file) << text;
  };
  std::error_code ec;
  make(t / "new1", "game.dat", "one");
  EXPECT(rekit::swap_in_folder((t / "new1").string(), (t / "out").string(), "ours.txt", ec));  // none there yet
  make(t / "out", "ours.txt", "marker");
  make(t / "new2", "game.dat", "two");
  make(t / "new2", "ours.txt", "marker");
  EXPECT(rekit::swap_in_folder((t / "new2").string(), (t / "out").string(), "ours.txt", ec) && !fs::exists(t / "out.old"));
  std::ifstream in(t / "out" / "game.dat");
  std::string got;
  in >> got;
  EXPECT(got == "two");
  make(t / "theirs", "photo.jpg", "keep me");
  make(t / "new3", "game.dat", "three");
  EXPECT(!rekit::swap_in_folder((t / "new3").string(), (t / "theirs").string(), "ours.txt", ec));
  EXPECT(ec == std::errc::directory_not_empty && fs::exists(t / "theirs" / "photo.jpg") && fs::exists(t / "new3"));
}
