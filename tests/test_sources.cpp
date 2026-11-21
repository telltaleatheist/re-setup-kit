// The source walk (rekit/sources.h) and the disc reader (rekit/disc.h) with a
// made-up recipe: "demo" (rank 5, an install's game/data.pak, a disc's
// setup.hdr) and "other" (rank 1, other.dat), over folders, images, cue
// sheets and archives this test writes.
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/disc.h"
#include "rekit/sources.h"

namespace fs = std::filesystem;

namespace {

rekit::Identity judge_prefix(const std::vector<uint8_t>& b, const std::string& magic, const char* game, int rank) {
  rekit::Identity id;
  if (b.size() >= magic.size() && std::string(b.begin(), b.begin() + magic.size()) == magic) {
    id.game = game, id.rank = rank;
    id.version = b.size() > magic.size() ? std::string(1, static_cast<char>(b[magic.size()])) : "";
  } else {
    id.why = "not the demo's";
  }
  return id;
}

rekit::SourceRules rules() {
  rekit::SourceRules r;
  r.clues.push_back({rekit::Clue::kInstall, "game/data.pak", 1u << 20,
                     [](const std::vector<uint8_t>& b) { return judge_prefix(b, "DEMO", "demo", 5); }});
  r.clues.push_back({rekit::Clue::kDisc, "setup.hdr", 1u << 20,
                     [](const std::vector<uint8_t>& b) { return judge_prefix(b, "DHDR", "demo", 5); }});
  r.clues.push_back({rekit::Clue::kInstall, "other.dat", 1u << 20,
                     [](const std::vector<uint8_t>& b) { return judge_prefix(b, "OTHR", "other", 1); }});
  r.programs = {"demo.exe"};
  return r;
}

std::string dir(const std::string& name) {
  const std::string d = std::string(REKIT_TEST_TMP) + "/" + name;
  fs::create_directories(d);
  return d;
}
void put(const std::string& p, const std::string& bytes) {
  fs::create_directories(fs::path(p).parent_path());
  std::ofstream(p, std::ios::binary) << bytes;
}

}  // namespace

TEST(sources_find_an_installed_folder_by_its_clue_in_any_case) {
  const std::string d = dir("install");
  put(d + "/Game/DATA.PAK", "DEMO2 rest");
  put(d + "/demo.exe", "MZ");
  for (const std::string& pick : {d, d + "/demo.exe"}) {
    const rekit::Found f = rekit::find_source(pick, rules());
    EXPECT(f.kind == rekit::Found::kInstall && f.game == "demo" && f.version == "2" && f.on_disk);
    EXPECT(f.path == d);
  }
  put(d + "/Game/DATA.PAK", "JUNK");
  const rekit::Found g = rekit::find_source(d, rules());
  EXPECT(g.kind == rekit::Found::kNothing && g.detail.find("not the demo's") != std::string::npos);
  const std::string found = rekit::any_case_path(d, "GAME/data.pak");
  EXPECT(!found.empty() && fs::equivalent(found, fs::path(d) / "Game" / "DATA.PAK"));
  EXPECT(rekit::any_case_path(d, "game/none.pak").empty());
}

TEST(sources_find_a_disc_folder) {
  const std::string d = dir("discdir");
  put(d + "/SETUP.HDR", "DHDR1");
  put(d + "/VIDEO/INTRO.AVI", "avi");
  const rekit::Found f = rekit::find_source(d, rules());
  EXPECT(f.kind == rekit::Found::kDisc && f.game == "demo" && f.version == "1" && f.on_disk);
  rekit::Disc disc;
  std::string err;
  EXPECT(disc.open(d, &err) && disc.kind() == "folder" && disc.files().size() == 2);
  const rekit::Disc::File* v = disc.find("video/intro.avi");
  uint8_t b[3];
  EXPECT(v && v->size == 3 && disc.read(*v, 0, 3, b) && std::string(b, b + 3) == "avi");
}

#if REKIT_HAVE_LIBARCHIVE
#include "archive_maker.h"

TEST(sources_find_disc_images_on_disk_and_a_cue_s_music) {
  const std::string d = dir("images");
  const std::string iso = write_archive("iso9660", {{"SETUP.HDR", "DHDR3"}, {"DATA/BIG.CAB", std::string(5000, 'c')}});
  put(d + "/demo.iso", iso);
  const rekit::Found a = rekit::find_source(d + "/demo.iso", rules());
  EXPECT(a.kind == rekit::Found::kDisc && a.game == "demo" && a.version == "3" && a.audio.empty());
  rekit::Disc disc;
  std::string err;
  EXPECT(disc.open(d + "/demo.iso", &err) && disc.kind() == "ISO image" && disc.find("data/big.cab"));
  // Raw MODE1/2352 data track plus two audio tracks, as Redump's sets.
  put(d + "/demo (Track 1).bin", raw_track(iso));
  put(d + "/demo (Track 2).bin", std::string(2352 * 100, '\0'));
  put(d + "/demo.cue",
      "FILE \"demo (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
      "FILE \"demo (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:00:02\n"
      "  TRACK 03 AUDIO\n    INDEX 01 00:01:00\n");
  const rekit::Found b = rekit::find_source(d + "/demo.cue", rules());
  EXPECT(b.kind == rekit::Found::kDisc && b.game == "demo" && b.audio.size() == 2);
  EXPECT(b.audio_track == std::vector<int>({2, 3}) && b.audio_start == std::vector<int64_t>({2 * 2352, 75 * 2352}));
  rekit::Disc raw;
  EXPECT(raw.open(d + "/demo.cue", &err) && raw.kind().find("raw image") == 0 && raw.find("setup.hdr"));
}

TEST(sources_walk_archives_in_archives_and_images_inside_them) {
  const std::string iso = write_archive("iso9660", {{"SETUP.HDR", "DHDR4"}, {"X.CAB", "x"}});
  // An install two archives deep: the 7z holds a zip holding the game's folder
  // (a 7z streams only as the outer file: it needs to seek).
  const std::string inner = write_archive("zip", {{"Demo Game/game/data.pak", "DEMO5"}, {"Demo Game/demo.exe", "MZ"}});
  const std::string zip = temp_file("deep.7z", write_archive("7zip", {{"readme.txt", "hi"}, {"pack/inner.zip", inner}}));
  const rekit::Found a = rekit::find_source(zip, rules());
  EXPECT(a.kind == rekit::Found::kInstall && a.game == "demo" && a.version == "5" && !a.on_disk);
  EXPECT(a.root == "Demo Game/" && a.steps.size() == 1 && a.steps[0].entry == "pack/inner.zip");
  // A raw image inside a 7z with its cue (after the .bin) naming an audio track.
  const std::string cue = "FILE \"d.bin\" BINARY\n TRACK 01 MODE1/2352\n  INDEX 01 00:00:00\n"
                          "FILE \"t2.bin\" BINARY\n TRACK 02 AUDIO\n  INDEX 01 00:00:00\n";
  const std::string img = temp_file("img.7z", write_archive("7zip", {{"cd/d.bin", raw_track(iso)},
                                                                     {"cd/t2.bin", std::string(2352 * 10, '\0')},
                                                                     {"cd/z.cue", cue}}));
  const rekit::Found b = rekit::find_source(img, rules());
  EXPECT(b.kind == rekit::Found::kDisc && b.game == "demo" && b.version == "4" && b.raw && b.raw_offset == 16);
  EXPECT(b.steps.size() == 1 && b.steps[0].kind == rekit::Step::kRawImage && b.data_track == "cd/d.bin");
  EXPECT(b.audio == std::vector<std::string>({"cd/t2.bin"}) && b.audio_track == std::vector<int>({2}));
}

TEST(sources_rank_the_recipe_s_best_and_an_install_over_a_disc) {
  const std::string z = temp_file("both.zip", write_archive("zip", {{"o/other.dat", "OTHR"},
                                                                    {"disc/SETUP.HDR", "DHDR1"},
                                                                    {"inst/game/data.pak", "DEMO1"},
                                                                    {"z/other.dat", "OTHR"}}));
  const rekit::Found f = rekit::find_source(z, rules());
  EXPECT(f.kind == rekit::Found::kInstall && f.game == "demo" && f.root == "inst/");
  const std::string o = temp_file("other.zip", write_archive("zip", {{"o/other.dat", "OTHR"}}));
  EXPECT(rekit::find_source(o, rules()).game == "other");
}

TEST(sources_stop_when_asked_and_say_what_was_read) {
  const std::string z = temp_file("stop.zip", write_archive("zip", {{"a.txt", "a"}, {"game/data.pak", "DEMO1"}}));
  int calls = 0;
  const rekit::Found f = rekit::find_source(z, rules(), [&](double, const std::string&) { return ++calls < 2; });
  EXPECT(f.cancelled && f.kind == rekit::Found::kNothing);
  const std::string j = temp_file("junk.zip", write_archive("zip", {{"game/data.pak", "NOPE"}}));
  const rekit::Found g = rekit::find_source(j, rules());
  EXPECT(g.kind == rekit::Found::kNothing && g.detail.find("game/data.pak: not the demo's") != std::string::npos);
  // A clue is a whole path at a folder: "mygame/data.pak" is not "game/data.pak".
  EXPECT(rekit::find_source(temp_file("near.zip", write_archive("zip", {{"mygame/data.pak", "DEMO1"}})), rules()).kind ==
         rekit::Found::kNothing);
  const rekit::Found n = rekit::find_source(temp_file("plain.txt", "hello"), rules());
  EXPECT(n.kind == rekit::Found::kNothing && n.detail.find("not an archive") != std::string::npos);
}
#endif
