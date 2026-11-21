// The archive layer (rekit/archive.h) read against
// archives this test writes with libarchive's own writer (nothing committed):
// zip, 7z, a zip inside a zip, an ISO 9660 image, the same as raw 2352-byte
// sectors, an ISO inside a 7z, and a damaged archive.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>


#include "check.h"
#include "rekit/archive.h"

#if REKIT_HAVE_LIBARCHIVE
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // archive.h brings <windows.h>, whose min/max macros break std::min
#endif
#include <archive.h>
#include <archive_entry.h>

#include "archive_maker.h"

namespace {


// Every file of a reader, read whole.
Files read_all(rekit::ArchiveReader& r) {
  Files out;
  rekit::ArchiveReader::Entry e;
  while (r.next(&e)) {
    if (e.dir) continue;
    std::string data;
    uint8_t buf[4096];
    for (int64_t n; (n = r.read(buf, sizeof buf)) > 0;) data.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    out[e.path] = data;
  }
  return out;
}

// Test files: a big one (several compressed blocks), a small one, one in a folder.
Files sample() {
  std::string big;
  for (int i = 0; i < 300000; ++i) big += static_cast<char>("MW3 rebuild "[i % 12] + (i / 997) % 7);
  return {{"zbd/reader.zbd", big}, {"Mech3.exe", "MZ fake exe"}, {"video/intro.avi", std::string(70000, 'v')}};
}

}  // namespace

TEST(archive_read_zip_and_7z_from_files) {
  const Files want = sample();
  for (const char* fmt : {"zip", "7zip"}) {
    const std::string path = temp_file(std::strcmp(fmt, "zip") == 0 ? "src.zip" : "src.7z", write_archive(fmt, want));
    rekit::ArchiveReader r;
    EXPECT(r.open_file(path));
    const Files got = read_all(r);
    EXPECT(got == want);
    std::printf("  %s: %zu files, format %s\n", fmt, got.size(), r.format().c_str());
  }
}

TEST(archive_read_a_zip_inside_a_zip_without_unpacking) {
  const Files want = sample();
  const std::string inner = write_archive("zip", want);
  const std::string outer =
      write_archive("zip", {{"MechWarrior 3 (USA).zip", inner}, {"scans/box front.jpg", std::string(5000, 'j')}});
  rekit::ArchiveReader r;
  EXPECT(r.open_stream(memory_stream(outer)));  // a stream: no seeking at all
  rekit::ArchiveReader::Entry e;
  bool found = false;
  while (r.next(&e)) {
    if (!rekit::archive_name(e.path)) continue;
    rekit::ArchiveReader inner_r;
    EXPECT(inner_r.open_stream(r.entry_stream()));
    found = read_all(inner_r) == want;
  }
  EXPECT(found);
}

TEST(archive_read_iso_images_cooked_raw_and_inside_7z) {
  const Files want = sample();
  // ISO 9660 keeps names in upper case 8.3 unless told otherwise; Rock Ridge
  // (libarchive's default) keeps them as written.
  const std::string iso = write_archive("iso9660", want);
  EXPECT(iso.size() % 2048 == 0 && iso.size() > 300000);
  {
    rekit::ArchiveReader r;
    EXPECT(r.open_iso_stream(memory_stream(iso)));
    EXPECT(read_all(r) == want);
  }
  {  // the same image as a .bin's raw sectors
    rekit::ArchiveReader r;
    EXPECT(r.open_iso_stream(rekit::cooked_sectors(memory_stream(raw_track(iso)))));
    EXPECT(read_all(r) == want);
  }
  {  // an .iso inside a 7z, read through the 7z's entry
    const std::string path = temp_file("iso.7z", write_archive("7zip", {{"MW3.iso", iso}}));
    rekit::ArchiveReader outer;
    EXPECT(outer.open_file(path));
    rekit::ArchiveReader::Entry e;
    EXPECT(outer.next(&e) && e.path == "MW3.iso");
    rekit::ArchiveReader r;
    EXPECT(r.open_iso_stream(outer.entry_stream()));
    EXPECT(read_all(r) == want);
  }
}

TEST(archive_refuse_damaged_and_unknown_files) {
  std::string zip = write_archive("zip", sample());
  zip.resize(zip.size() / 2);  // cut short: no central directory
  rekit::ArchiveReader r;
  const bool opened = r.open_file(temp_file("cut.zip", zip));
  Files got;
  if (opened) got = read_all(r);
  EXPECT(!opened || got != sample());
  EXPECT(!r.error().empty() || !opened || got.size() < 3);
  rekit::ArchiveReader t;
  EXPECT(!t.open_file(temp_file("plain.txt", "not an archive at all, just text\n")));
  EXPECT(rekit::archive_name("x.ZIP") && rekit::archive_name("a.tar.gz") && !rekit::archive_name("track.bin"));
}

#else

TEST(archives_not_in_this_build) { EXPECT(!rekit::archives_supported()); }

#endif
