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
