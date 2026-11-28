// The install record CHECK MY FILES reads (manifest.h): written, read, checked.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/manifest.h"

namespace fs = std::filesystem;

namespace {
std::string tmp(const std::string& name) { return (fs::path(REKIT_TEST_TMP) / "manifest" / name).string(); }
void put(const std::string& path, const std::string& bytes) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path, std::ios::binary) << bytes;
}
std::string read(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
}  // namespace

// Every file but the record and what is left out, sorted, in sha256sum's format; checked as installed; a file added
// later (a mod) is not looked at; a changed and a missing one are named.
TEST(manifest_records_and_checks_the_files) {
  const std::string d = tmp("game");
  put(d + "/b.dat", "bee"), put(d + "/sub/a.txt", "a"), put(d + "/NEV_UNIV.EXE", "tool");
  const std::vector<rekit::LeaveOut> leave{{"nev_univ.exe", ""}};
  std::string why;
  int calls = 0;
  EXPECT(rekit::write_manifest(d, leave, [&calls](int, int, const std::string&) { return ++calls, true; }, &why));
  EXPECT(read(d + "/" + rekit::kFilesManifest) ==  // sha256("bee"), sha256("a")
         "62cb81b5904a262ffaeed02abef36bfc540b09f964b8b0b636662f77ffce6714  b.dat\n"
         "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb  sub/a.txt\n");
  std::vector<rekit::ManifestEntry> list;
  EXPECT(rekit::parse_manifest(read(d + "/" + rekit::kFilesManifest), &list) && list.size() == 2 && list[0].path == "b.dat" &&
         list[1].path == "sub/a.txt" && list[1].sha256 == "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
  EXPECT(calls == 2 && !fs::exists(d + "/" + rekit::kFilesManifest + ".part"));
  rekit::FilesCheck c = rekit::check_files(d, leave, nullptr);
  EXPECT(c.ok() && c.checked == 2);
  put(d + "/mods/new.txt", "mod");  // added later: not looked at
  EXPECT(rekit::check_files(d, leave, nullptr).ok());
  put(d + "/b.dat", "changed");
  fs::remove(d + "/sub/a.txt");
  c = rekit::check_files(d, leave, nullptr);
  EXPECT(!c.ok() && c.checked == 0 && c.changed == std::vector<std::string>{"b.dat"} &&
         c.missing == std::vector<std::string>{"sub/a.txt"});
  // A record naming a file the recipe leaves out: that file gone is not damage.
  put(d + "/b.dat", "bee"), put(d + "/sub/a.txt", "a");
  EXPECT(rekit::write_manifest(d, {}, nullptr, &why) && read(d + "/" + rekit::kFilesManifest).find("NEV_UNIV.EXE") != std::string::npos);
  fs::remove(d + "/NEV_UNIV.EXE");
  EXPECT(rekit::check_files(d, leave, nullptr).ok() && !rekit::check_files(d, {}, nullptr).ok());
  // Cancelled: no record written, the old one kept.
  const std::string before = read(d + "/" + rekit::kFilesManifest);
  bool cancelled = false;
  EXPECT(!rekit::write_manifest(d, {}, [](int, int, const std::string&) { return false; }, &why, &cancelled) && cancelled);
  EXPECT(read(d + "/" + rekit::kFilesManifest) == before);
}

// What is not a record: a short sum, a path that leaves the folder, an absolute one. sha256sum's binary mode is read.
// No record: nothing to check against; a bad one: an error, not damage.
TEST(manifest_refuses_what_is_not_one) {
  const std::string sum(64, 'a');
  std::vector<rekit::ManifestEntry> list;
  EXPECT(rekit::parse_manifest(sum + "  a/b.txt\r\n" + sum + " *c.txt\n\n", &list) && list.size() == 2 && list[1].path == "c.txt");
  EXPECT(!rekit::parse_manifest(std::string(63, 'a') + "  a.txt\n", &list) && list.empty());
  EXPECT(!rekit::parse_manifest(sum + "  ../a.txt\n", &list) && !rekit::parse_manifest(sum + "  /etc/a\n", &list));
  EXPECT(!rekit::parse_manifest(std::string(64, 'g') + "  a.txt\n", &list) && !rekit::parse_manifest(sum + " a.txt\n", &list));
  const std::string d = tmp("none");
  fs::create_directories(d);
  rekit::FilesCheck c = rekit::check_files(d, {}, nullptr);
  EXPECT(!c.has_manifest && !c.ok());
  put(d + "/" + rekit::kFilesManifest, "not a record\n");
  c = rekit::check_files(d, {}, nullptr);
  EXPECT(c.has_manifest && !c.error.empty() && c.missing.empty() && !c.ok());
}
