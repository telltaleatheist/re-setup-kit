// A game's original sources read from a catalog (rekit/original.h): the entry's list, each source checked.
#include <string>
#include <vector>

#include "check.h"
#include "rekit/original.h"

namespace {
const std::string kSum = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
std::string src(const std::string& url, const std::string& sum = kSum, const std::string& size = "1000", const std::string& name = "Demo (1999).zip") {
  return "{\"url\": \"" + url + "\", \"sha256\": \"" + sum + "\", \"size\": " + size + ", \"name\": \"" + name + "\", \"note\": \"a disc image\"}";
}
std::string catalog(const std::string& original, const std::string& key = "demo") {
  return "{\"revision\": 4, \"games\": [{\"key\": \"other\", \"original\": [" + src("https://a.example/o.zip") + "]}, {\"key\": \"" + key +
         "\", \"name\": \"Demo\"" + (original.empty() ? "" : ", \"original\": " + original) + "}]}";
}
}  // namespace

TEST(original_reads_the_entrys_sources) {
  std::vector<rekit::OriginalSource> s;
  std::string why;
  const std::string upper = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
  EXPECT(rekit::read_original(catalog("[" + src("https://a.example/d.zip", upper) + ", " + src("https://b.example/x/d.zip") + "]"), "demo", false, &s, &why));
  EXPECT(s.size() == 2 && s[0].url == "https://a.example/d.zip" && s[0].sha256 == kSum && s[0].size == 1000 &&
         s[0].name == "Demo (1999).zip" && s[0].note == "a disc image" && s[1].url == "https://b.example/x/d.zip");
  // file:/// only when allowed (checks).
  EXPECT(!rekit::read_original(catalog("[" + src("file:///t/d.zip") + "]"), "demo", false, &s, &why) && s.empty());
  EXPECT(rekit::read_original(catalog("[" + src("file:///t/d.zip") + "]"), "demo", true, &s, &why) && s.size() == 1);
}

TEST(original_refuses_what_is_not_a_good_source) {
  std::vector<rekit::OriginalSource> s;
  std::string why;
  EXPECT(!rekit::read_original(catalog(""), "demo", false, &s, &why) && why == "the catalog lists no source of the game's files");
  EXPECT(!rekit::read_original(catalog("[]"), "nobody", false, &s, &why) && why == "the catalog has no entry nobody");
  EXPECT(!rekit::read_original("{\"games\": [", "demo", false, &s, &why) && why == "the catalog is not JSON");
  EXPECT(!rekit::read_original("{\"games\": {}}", "demo", false, &s, &why) && why == "the catalog has no games list");
  const std::string good = src("https://a.example/d.zip");
  const std::vector<std::string> bad = {
      "[]", "{}", "\"x\"", "[" + good + "," + good + "," + good + "," + good + "," + good + "]",
      "[" + good + ", " + src("http://a.example/d.zip") + "]", "[" + src("https://a.example/../d.zip") + "]",
      "[" + src("https://user@a.example/d.zip") + "]", "[" + src("https://localhost/d.zip") + "]",
      "[" + src("https://a.example/d zip") + "]", "[" + src("ftp://a.example/d.zip") + "]",
      "[" + src("http://www.example.com/d.zip") + "]",  // http's host read where https's starts is a plain one
      "[" + src("https://a.example/d.zip", "abc") + "]", "[" + src("https://a.example/d.zip", std::string(64, 'g')) + "]",
      "[" + src("https://a.example/d.zip", kSum, "0") + "]", "[" + src("https://a.example/d.zip", kSum, "-5") + "]",
      "[" + src("https://a.example/d.zip", kSum, "\"9\"") + "]", "[" + src("https://a.example/d.zip", kSum, "1.5") + "]",
      "[" + src("https://a.example/d.zip", kSum, "1000", "../d.zip") + "]", "[" + src("https://a.example/d.zip", kSum, "1000", "a/b.zip") + "]",
      "[" + src("https://a.example/d.zip", kSum, "1000", ".hidden") + "]", "[{\"url\": \"https://a.example/d.zip\"}]",
  };
  for (const std::string& o : bad) {
    const bool read = rekit::read_original(catalog(o), "demo", false, &s, &why);
    EXPECT(!read && s.empty() && !why.empty());
    if (read) std::printf("  taken: %s\n", o.c_str());
  }
}

TEST(original_reads_compact_sources_after_the_originals_by_game_version) {
  std::vector<rekit::OriginalSource> s;
  std::string why;
  const auto entry = [](const std::string& fields) {
    return "{\"games\": [{\"key\": \"demo\"" + fields + "}]}";
  };
  const auto compact = [](const std::string& url, const std::string& game_min) {
    std::string j = src(url);
    if (!game_min.empty()) j.insert(j.size() - 1, ", \"game_min\": \"" + game_min + "\"");
    return j;
  };
  const std::string both = entry(", \"original\": [" + src("https://a.example/disc.zip") + "], \"compact\": [" +
                                 compact("https://b.example/c.tar.xz", "1.7.0") + ", " + compact("https://b.example/any.tar.xz", "") + "]");
  // A new enough game: the original first, then both compacts, marked.
  EXPECT(rekit::read_original(both, "demo", false, &s, &why, "1.7.0") && s.size() == 3);
  EXPECT(!s[0].compact && s[1].compact && s[1].url == "https://b.example/c.tar.xz" && s[2].url == "https://b.example/any.tar.xz");
  EXPECT(rekit::read_original(both, "demo", false, &s, &why, "1.10") && s.size() == 3);  // 1.10 > 1.7.0
  EXPECT(rekit::read_original(both, "demo", false, &s, &why, "pm-v1.8.0-3-gabc") && s.size() == 3);  // a release tag
  // Older, or unknown: the game_min one is left out; the one without game_min stays.
  EXPECT(rekit::read_original(both, "demo", false, &s, &why, "1.6.9") && s.size() == 2 && s[1].url == "https://b.example/any.tar.xz");
  EXPECT(rekit::read_original(both, "demo", false, &s, &why) && s.size() == 2);
  // Only a compact, too new for this game: said.
  const std::string only = entry(", \"compact\": [" + compact("https://b.example/c.tar.xz", "2.0") + "]");
  EXPECT(!rekit::read_original(only, "demo", false, &s, &why, "1.9") && s.empty() &&
         why == "the catalog lists no source this version of the game reads");
  EXPECT(rekit::read_original(only, "demo", false, &s, &why, "2.0.0") && s.size() == 1);
  // A bad game_min spoils its list only; the originals still come.
  const std::string bad_min = entry(", \"original\": [" + src("https://a.example/disc.zip") + "], \"compact\": [" +
                                    compact("https://b.example/c.tar.xz", "one") + "]");
  EXPECT(rekit::read_original(bad_min, "demo", false, &s, &why, "1.0") && s.size() == 1 && !s[0].compact);
  EXPECT(!rekit::read_original(entry(", \"compact\": [" + compact("https://b.example/c.tar.xz", "one") + "]"), "demo", false, &s, &why, "1.0") &&
         why.find("game_min one is not a version") != std::string::npos);
}

TEST(original_compares_versions_by_number) {
  EXPECT(rekit::compare_versions("1.10.0", "1.9") == 1 && rekit::compare_versions("1.9", "1.10.0") == -1);
  EXPECT(rekit::compare_versions("1.2", "1.2.0") == 0 && rekit::compare_versions("2", "1.99.99") == 1);
  EXPECT(rekit::version_number("pm-v1.2.12") == "1.2.12" && rekit::version_number("v1.6.1-4-gabc12") == "1.6.1" &&
         rekit::version_number("1.6.1") == "1.6.1" && rekit::version_number("mw1-v1.1.4") == "1.1.4" && rekit::version_number("dev") == "");
  EXPECT(rekit::compare_versions("1.x", "1") == -2 && rekit::compare_versions("", "1") == -2 && rekit::compare_versions("1..2", "1") == -2);
}
