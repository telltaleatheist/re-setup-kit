// Names that aren't text in the program's code page (Windows without the UTF-8 manifest, as this test program; or not
// valid UTF-16 at all), near where the kit looks: passed over, never a throw. Elsewhere any name is text.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/disc.h"
#include "rekit/family_folder.h"
#include "rekit/files.h"
#include "rekit/search.h"
#include "rekit/sources.h"

namespace fs = std::filesystem;

namespace {

fs::path odd_folder(const char* name) {
  const fs::path d = fs::temp_directory_path() / ("rekit-names-" + std::string(name));
  std::error_code ec;
  fs::remove_all(d, ec);
  std::vector<fs::path> odd = {fs::path(u"日"), fs::path(u"é")};
#if defined(_WIN32)
  odd.push_back(fs::path(L"\xD800"));  // half a pair: text in no code page
#endif
  for (const fs::path& o : odd) {
    fs::path file = "log ", folder = "Spiel ";
    file += o, folder += o;
    file += ".txt";
    fs::create_directories(d / folder);
    std::ofstream(d / file) << "odd";
    std::ofstream(d / folder / "inside.txt") << "odd";
  }
  std::ofstream(d / "btech.exe") << "game";
  fs::create_directories(d / "Game");
  std::ofstream(d / "Game" / "btech.exe") << "game";
  return d;
}

}  // namespace

TEST(names_outside_the_code_page_are_passed_over) {
  const fs::path d = odd_folder("walk");
  const std::string at = rekit::any_case_path(d.string(), "BTECH.EXE");
  EXPECT(!at.empty() && fs::exists(at));
  EXPECT(rekit::any_case_path(d.string(), "NONE.EXE").empty());

  rekit::SearchHints h;
  h.drop_in = true;
  const std::vector<std::string> c = rekit::install_candidates(h, d.string());
  EXPECT(std::find(c.begin(), c.end(), (d / "Game").string()) != c.end());

  rekit::Disc disc;
  std::string err;
  EXPECT(disc.open(d.string(), &err) && disc.find("Game/btech.exe") != nullptr);

  rekit::SourceRules rules;
  EXPECT(rekit::find_source(d.string(), rules, nullptr).kind == rekit::Found::kNothing);
}

TEST(names_outside_the_code_page_do_not_stop_a_copy_or_a_move) {
  const fs::path d = odd_folder("copy"), to = d.parent_path() / "rekit-names-copied", moved = d.parent_path() / "rekit-names-moved";
  std::error_code ec;
  fs::remove_all(to, ec);
  fs::remove_all(moved, ec);
  const rekit::CopyResult r = rekit::copy_folder(d.string(), to.string(), nullptr);
  EXPECT(r.ok && fs::exists(to / "btech.exe") && fs::exists(to / "Game" / "btech.exe"));
  // A folder with nothing but such a name has something of the player's in it: moved, with it.
  fs::remove_all(d / "Game", ec);
  fs::remove(d / "btech.exe", ec);
  EXPECT(rekit::move_once(d.string(), moved.string(), {}).kind == rekit::FolderMove::kMoved);
  EXPECT(!fs::is_empty(moved, ec));
}
