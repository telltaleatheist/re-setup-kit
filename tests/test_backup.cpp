// The player's files backed up before an update or a settings reset (rekit/backup.h): what goes where, only the
// newest of a kind kept, only the kit's own folders removed, and a failure leaving nothing.
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/backup.h"

namespace fs = std::filesystem;

namespace {
void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
std::string read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
std::vector<std::string> backups(const fs::path& user) {
  std::vector<std::string> out;
  for (const auto& e : fs::directory_iterator(user / "Backups")) out.push_back(e.path().filename().string());
  std::sort(out.begin(), out.end());
  return out;
}
}  // namespace

TEST(backup_copies_files_and_folders_in_place) {
  const fs::path user = fs::path(REKIT_TEST_TMP) / "backup-user", other = fs::path(REKIT_TEST_TMP) / "backup-elsewhere.cfg";
  put(user / "saves" / "a.sav", "A"), put(user / "saves" / "deep" / "b.sav", "B"), put(user / "settings.cfg", "S");
  put(other, "O");
  const rekit::Backup b = rekit::back_up(user.string(), {"saves", (user / "settings.cfg").string(), other.string(), "gone.txt"}, "before 1.7.1", 3);
  EXPECT(b.ok && b.error.empty());
  const fs::path d(b.folder);
  EXPECT(d.parent_path() == user / "Backups" && d.filename().string().size() == 19 + 1 + 12 &&
         d.filename().string().substr(19) == " before 1.7.1");
  EXPECT(read(d / "saves" / "a.sav") == "A" && read(d / "saves" / "deep" / "b.sav") == "B" && read(d / "settings.cfg") == "S");
  EXPECT(read(d / "backup-elsewhere.cfg") == "O" && !fs::exists(d / "gone.txt") && fs::exists(d / rekit::kBackupMark));
  EXPECT(!fs::exists(d.string() + ".part"));
}

// Only the newest `keep` of the same kind stay; another kind, and a folder that is not the kit's, are never removed.
TEST(backup_keeps_the_newest_of_its_kind) {
  const fs::path user = fs::path(REKIT_TEST_TMP) / "backup-keep";
  put(user / "settings.cfg", "S");
  const fs::path base = user / "Backups";
  for (const char* old : {"2020-01-01 10.00.00 settings", "2020-01-02 10.00.00 settings", "2020-01-03 10.00.00 settings"})
    put(base / old / rekit::kBackupMark, "x");
  put(base / "2019-01-01 10.00.00 settings" / "mine.txt", "the player's");          // no mark: never touched
  put(base / "2019-01-01 10.00.00 settings extra" / rekit::kBackupMark, "x");       // another kind
  put(base / "2019-01-01 10.00.00 before 1.0" / rekit::kBackupMark, "x");           // another kind
  const rekit::Backup b = rekit::back_up(user.string(), {"settings.cfg"}, "settings", 2);
  EXPECT(b.ok);
  const std::vector<std::string> left = backups(user);
  EXPECT(left.size() == 5);
  EXPECT(std::count(left.begin(), left.end(), "2020-01-03 10.00.00 settings") == 1 &&
         std::count(left.begin(), left.end(), "2020-01-02 10.00.00 settings") == 0 &&
         std::count(left.begin(), left.end(), "2020-01-01 10.00.00 settings") == 0);
  EXPECT(read(base / "2019-01-01 10.00.00 settings" / "mine.txt") == "the player's");
  EXPECT(fs::exists(base / "2019-01-01 10.00.00 settings extra") && fs::exists(base / "2019-01-01 10.00.00 before 1.0"));
  EXPECT(std::count(left.begin(), left.end(), fs::path(b.folder).filename().string()) == 1);
}

#if !defined(_WIN32)
// A file that cannot be read: no backup, nothing left behind.
TEST(backup_failure_leaves_nothing) {
  const fs::path user = fs::path(REKIT_TEST_TMP) / "backup-fail";
  put(user / "saves" / "a.sav", "A");
  put(user / "saves" / "locked.sav", "L");
  fs::permissions(user / "saves" / "locked.sav", fs::perms::none);
  const bool root_reads = std::ifstream(user / "saves" / "locked.sav").good();  // root reads anything: no failure to see
  const rekit::Backup b = rekit::back_up(user.string(), {"saves"}, "settings", 3);
  fs::permissions(user / "saves" / "locked.sav", fs::perms::owner_all);
  if (root_reads) return;
  EXPECT(!b.ok && !b.error.empty() && b.folder.empty());
  EXPECT(backups(user).empty());
}
#endif
