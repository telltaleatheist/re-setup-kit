// ZIP, raw deflate, the sums, and InstallShield 5 cabinets (rekit/zip.h, hash.h, installshield.h).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/hash.h"
#include "rekit/installshield.h"
#include "rekit/zip.h"

TEST(hash_known_answers) {
  const char* abc = "abc";
  EXPECT(rekit::sha256_hex(reinterpret_cast<const uint8_t*>(abc), 3) ==
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  rekit::Sha256 pieces;
  pieces.add(reinterpret_cast<const uint8_t*>("a"), 1);
  pieces.add(reinterpret_cast<const uint8_t*>("bc"), 2);
  EXPECT(pieces.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  uint8_t m[16];
  rekit::md5(reinterpret_cast<const uint8_t*>(abc), 3, m);
  char hex[33];
  for (int i = 0; i < 16; ++i) std::snprintf(hex + 2 * i, 3, "%02x", m[i]);
  EXPECT(std::string(hex) == "900150983cd24fb0d6963f7d28e17f72");
  EXPECT(rekit::zip_crc(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);
}

// A stored and a deflated entry in a ZIP written here.
TEST(zip_reads_stored_and_deflated_entries) {
  // raw deflate of "hello hello hello hello\n" (zlib, wbits -15)
  const uint8_t def[] = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0xb9, 0x00};
  const std::string want = "hello hello hello hello\n";
  std::vector<uint8_t> out;
  EXPECT(rekit::inflate_raw(def, sizeof def, &out, want.size()) && std::string(out.begin(), out.end()) == want);
  // A ZIP with the two entries, its central directory at the end.
  auto le16 = [](std::string& s, uint32_t v) { s += char(v & 255), s += char(v >> 8 & 255); };
  auto le32 = [&](std::string& s, uint32_t v) { le16(s, v & 0xffff), le16(s, v >> 16); };
  struct E { std::string name, data; uint16_t method; uint32_t usize; uint32_t local = 0; };
  std::vector<E> es = {{"stored.txt", "plain bytes", 0, 11}, {"dir/deflated.txt", std::string((const char*)def, sizeof def), 8, (uint32_t)want.size()}};
  const uint32_t crcs[2] = {rekit::zip_crc((const uint8_t*)"plain bytes", 11), rekit::zip_crc((const uint8_t*)want.data(), want.size())};
  std::string z;
  for (size_t i = 0; i < es.size(); ++i) {
    E& e = es[i];
    e.local = (uint32_t)z.size();
    le32(z, 0x04034b50), le16(z, 20), le16(z, 0), le16(z, e.method), le32(z, 0), le32(z, crcs[i]), le32(z, (uint32_t)e.data.size()),
        le32(z, e.usize), le16(z, (uint16_t)e.name.size()), le16(z, 0);
    z += e.name + e.data;
  }
  const uint32_t cd = (uint32_t)z.size();
  for (size_t i = 0; i < es.size(); ++i) {
    E& e = es[i];
    le32(z, 0x02014b50), le16(z, 20), le16(z, 20), le16(z, 0), le16(z, e.method), le32(z, 0), le32(z, crcs[i]),
        le32(z, (uint32_t)e.data.size()), le32(z, e.usize), le16(z, (uint16_t)e.name.size()), le16(z, 0), le16(z, 0), le16(z, 0),
        le16(z, 0), le32(z, 0), le32(z, e.local);
    z += e.name;
  }
  const uint32_t cd_size = (uint32_t)z.size() - cd;
  le32(z, 0x06054b50), le16(z, 0), le16(z, 0), le16(z, 2), le16(z, 2), le32(z, cd_size), le32(z, cd), le16(z, 0);
  const std::string path = std::string(REKIT_TEST_TMP) + "/two.zip";
  std::ofstream(path, std::ios::binary) << z;
  rekit::Zip zip;
  EXPECT(zip.open(path) && zip.entries.size() == 2);
  for (const rekit::Zip::Entry& en : zip.entries) {
    std::vector<uint8_t> data;
    std::string err;
    EXPECT(zip.read(en, &data, &err));
    const std::string got(data.begin(), data.end());
    EXPECT(got == (en.name == "stored.txt" ? std::string("plain bytes") : want));
  }
}

// A real cabinet when one is given (REKIT_IS5_DIR: a folder with data1.hdr and data1.cab, any case), every file
// MD5-checked by the reader; none is committed.
TEST(installshield_reads_a_real_cabinet_when_given) {
  const char* dir = std::getenv("REKIT_IS5_DIR");
  if (!dir) return (void)std::printf("  (skipped: REKIT_IS5_DIR not set)\n");
  namespace fs = std::filesystem;
  auto slurp = [](const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  fs::path hdr, cab;
  for (const auto& e : fs::directory_iterator(dir)) {
    std::string n = e.path().filename().string();
    for (char& c : n) c = (char)std::tolower((unsigned char)c);
    if (n == "data1.hdr") hdr = e.path();
    if (n == "data1.cab") cab = e.path();
  }
  EXPECT(!hdr.empty() && !cab.empty());
  std::vector<std::pair<std::string, uint64_t>> list;
  std::string err;
  EXPECT(rekit::list_installshield(slurp(hdr), &list, &err));
  std::vector<rekit::CabFile> files;
  EXPECT(rekit::read_installshield(slurp(hdr), slurp(cab), &files, &err));
  std::printf("  %zu files listed, %zu read%s%s\n", list.size(), files.size(), err.empty() ? "" : ": ", err.c_str());
  EXPECT(!files.empty() && files.size() == list.size());
}
