// The program's record for a launcher (rekit/program_record.h): written once, the same lines as before, read back,
// and never for a copy a launcher installed.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "check.h"
#include "rekit/program_record.h"

namespace fs = std::filesystem;

TEST(program_record_written_read_and_not_for_a_launchers_copy) {
  const fs::path dir = fs::path(REKIT_TEST_TMP) / "program_record";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  const std::string self = rekit::self_path();
  std::printf("  this program: %s\n", self.c_str());
  EXPECT(!self.empty() && fs::is_regular_file(self, ec));
  EXPECT(!rekit::launchers_own(self));  // the build folder has no mark above it
  EXPECT(rekit::record_program(dir.string(), "demo", "demo-v1.2.3"));
  std::ifstream f(dir / rekit::kProgramRecord, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  EXPECT(text == "# Where this game is, for the BattleTech launcher (it links a copy installed without it).\ngame=demo\nprogram=" + self +
                     "\nversion=demo-v1.2.3\n");
  const rekit::ProgramRecord r = rekit::read_program_record(dir.string());
  EXPECT(r.game == "demo" && r.program == self && r.version == "demo-v1.2.3");
  EXPECT(rekit::read_program_record((dir / "none").string()).game.empty() && !rekit::record_program("", "demo", "1"));
  // A program inside a game folder a launcher made (its mark two folders up) is the launcher's.
  const fs::path game = dir / "games" / "demo";
  fs::create_directories(game / "bin", ec);
  std::ofstream(game / rekit::kInstalledMark) << "game=demo\n";
  std::ofstream(game / "bin" / "demo") << "x";
  EXPECT(rekit::launchers_own((game / "bin" / "demo").string()) && !rekit::launchers_own((dir / "elsewhere").string()));
}
