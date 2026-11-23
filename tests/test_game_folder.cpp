// A game's own folder and its mark (rekit/game_folder.h): the same files and lines as mw3-re writes.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "check.h"
#include "rekit/game_folder.h"

namespace fs = std::filesystem;

namespace {
std::string folder(const std::string& name) { return (fs::path(REKIT_TEST_TMP) / "game-folder" / name).string(); }
std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
void put(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}
size_t count(const fs::path& d) {
  std::error_code ec;
  return static_cast<size_t>(std::distance(fs::directory_iterator(d, ec), fs::directory_iterator()));
}
}  // namespace

TEST(game_folder_names_match_mw3_re) {
  EXPECT(std::string(rekit::kGameFolderMark) == "rebuild-game-folder.txt");
  EXPECT(std::string(rekit::kGamesFolderMark) == "rebuild-games.txt");
}

TEST(game_folder_in_a_pick) {
  const std::string missing = folder("missing"), empty = folder("empty"), full = folder("full"), mine = folder("mine"),
                    other = folder("other");
  fs::create_directories(empty);
  put(fs::path(full) / "notes.txt", "someone's");
  put(fs::path(mine) / rekit::kGameFolderMark, "# x\ngame=demo\n");
  put(fs::path(other) / rekit::kGameFolderMark, "# x\ngame=other\n");
  EXPECT(rekit::game_folder_in(missing, "demo", "Demo Game") == missing);
  EXPECT(rekit::game_folder_in(empty, "demo", "Demo Game") == empty);
  EXPECT(rekit::game_folder_in(mine, "demo", "Demo Game") == mine);
  EXPECT(rekit::game_folder_in(full, "demo", "Demo Game") == (fs::path(full) / "Demo Game").string());
  // Another game's folder, or a key that only starts the same: a folder of this game's own inside it.
  EXPECT(rekit::game_folder_in(other, "demo", "Demo Game") == (fs::path(other) / "Demo Game").string());
  put(fs::path(folder("prefix")) / rekit::kGameFolderMark, "game=demo2\n");
  EXPECT(rekit::game_folder_in(folder("prefix"), "demo", "Demo Game") == (fs::path(folder("prefix")) / "Demo Game").string());
}

TEST(game_folder_claim_writes_both_marks) {
  const fs::path d = folder("claimed/Demo Game");
  std::string error;
  EXPECT(rekit::claim_game_folder(d.string(), "demo", &error) && error.empty());
  const std::string mark = read(d / rekit::kGameFolderMark);
  EXPECT(mark.find("\ngame=demo\n") != std::string::npos && mark.rfind("# ", 0) == 0);
  EXPECT(fs::is_regular_file(d / rekit::kGamesFolderMark) && count(d) == 2);
  EXPECT(rekit::claim_game_folder(d.string(), "demo", &error));  // again: already this game's
  // A folder mw3-re made is read the same: its mark line is the same.
  const fs::path m = folder("from-mw3-re");
  put(m / rekit::kGameFolderMark, "# A game's folder, made by the MechWarrior 3 Rebuild launcher or the game's setup.\ngame=mw3\n");
  EXPECT(rekit::claim_game_folder(m.string(), "mw3", &error) && rekit::game_folder_in(m.string(), "mw3", "x") == m.string());
}

// Break tests: nothing is written into a folder that is not the game's.
TEST(game_folder_claim_refuses_what_is_not_ours) {
  std::string error;
  const fs::path full = folder("refuse/full");
  put(full / "photo.jpg", "x");
  EXPECT(!rekit::claim_game_folder(full.string(), "demo", &error) && error.find("not this game's") != std::string::npos);
  EXPECT(count(full) == 1 && !fs::exists(full / rekit::kGameFolderMark));
  const fs::path other = folder("refuse/other");
  put(other / rekit::kGameFolderMark, "game=other\n");
  error.clear();
  EXPECT(!rekit::claim_game_folder(other.string(), "demo", &error) && error.find("another game's") != std::string::npos);
  EXPECT(read(other / rekit::kGameFolderMark) == "game=other\n" && count(other) == 1);
  const fs::path file = folder("refuse/a-file");
  put(file, "x");
  error.clear();
  EXPECT(!rekit::claim_game_folder(file.string(), "demo", &error) && !error.empty() && fs::is_regular_file(file));
  error.clear();
  EXPECT(!rekit::claim_game_folder("", "demo", &error) && !error.empty());
}

TEST(game_folder_media_beside_a_marked_install) {
  const fs::path game = folder("media/Demo Game"), user = folder("media-user");
  std::string error;
  EXPECT(rekit::claim_game_folder(game.string(), "demo", &error));
  EXPECT(rekit::media_dir((game / "install").string(), user.string()) == game.string());
  EXPECT(rekit::media_dir((user / "install").string(), user.string()) == user.string());
  EXPECT(rekit::media_dir("", user.string()) == user.string());
}
