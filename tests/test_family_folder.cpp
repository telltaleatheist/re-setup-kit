// The family's folders moved from MicroProse to BattleTech once (rekit/family_folder.h).
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "rekit/family_folder.h"

namespace fs = std::filesystem;

TEST(family_folder_old_name_beside_the_new) {
  const fs::path base = fs::path("/Users/x/Library/Application Support");
  EXPECT(fs::path(rekit::old_org_folder((base / "BattleTech" / "MechWarrior 3").string())) == base / "MicroProse" / "MechWarrior 3");
  EXPECT(fs::path(rekit::old_org_folder((base / "BattleTech" / "Pirate's Moon").string() + "/")) == base / "MicroProse" / "Pirate's Moon");
  EXPECT(rekit::old_org_folder((base / "Elsewhere" / "MechWarrior 3").string()).empty());
}

TEST(family_folder_moves_once_and_never_over_a_folder_in_use) {
  const fs::path t = fs::path(REKIT_TEST_TMP) / "family";
  std::error_code ec;
  fs::remove_all(t, ec);
  const fs::path from = t / "MicroProse" / "MechWarrior 3", to = t / "BattleTech" / "MechWarrior 3";
  fs::create_directories(from / "pilots");
  std::ofstream(from / "pilots" / "a.plt") << "pilot";
  fs::create_directories(to);  // empty, as SDL_GetPrefPath makes it
  rekit::FolderMove m = rekit::move_once(from.string(), to.string(), {"data-folder.txt"});
  EXPECT(m.kind == rekit::FolderMove::kMoved && !fs::exists(from) && fs::exists(to / "pilots" / "a.plt"));
  // A pointer back left in the old place: nothing more to move.
  fs::create_directories(from);
  std::ofstream(from / "data-folder.txt") << to.string() << "\n";
  std::ofstream(from / ".DS_Store") << "x";
  m = rekit::move_once(from.string(), to.string(), {"data-folder.txt"});
  EXPECT(m.kind == rekit::FolderMove::kNothing && fs::exists(to / "pilots" / "a.plt") && fs::exists(from / "data-folder.txt"));
  // Something new in both: never merged, the new one stays in use.
  std::ofstream(from / "mw3.cfg") << "x=1\n";
  m = rekit::move_once(from.string(), to.string(), {"data-folder.txt"});
  EXPECT(m.kind == rekit::FolderMove::kNothing && fs::exists(from / "mw3.cfg") && !fs::exists(to / "mw3.cfg"));
  // Nothing in the old place: nothing to do.
  EXPECT(rekit::move_once((t / "MicroProse" / "None").string(), (t / "BattleTech" / "None").string()).kind == rekit::FolderMove::kNothing);
  EXPECT(!fs::exists(t / "BattleTech" / "None"));
}

TEST(family_folder_removes_only_empty_folders) {
  const fs::path t = fs::path(REKIT_TEST_TMP) / "family_empty";
  std::error_code ec;
  fs::remove_all(t, ec);
  fs::create_directories(t / "MicroProse" / "BattleTech" / "games");
  std::ofstream(t / "keep.txt") << "x";
  rekit::remove_if_empty((t / "MicroProse" / "BattleTech" / "games").string(), 3);
  EXPECT(!fs::exists(t / "MicroProse") && fs::exists(t / "keep.txt"));  // three empty folders, then one in use
  fs::create_directories(t / "MicroProse" / "Other Game");
  rekit::remove_if_empty((t / "MicroProse" / "BattleTech").string(), 2);
  EXPECT(fs::exists(t / "MicroProse" / "Other Game"));  // another publisher's game: kept
}
