#include "rekit/original.h"

#include "json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace rekit {
namespace {

using detail::Json;
using detail::Reader;

bool plain_host(const std::string& h) {
  if (h.empty() || h.size() > 253 || h.find('.') == std::string::npos || h.find("..") != std::string::npos || h.front() == '.' ||
      h.back() == '.' || h.front() == '-')
    return false;
  for (char c : h)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.') return false;
  return true;
}

// https:// and a plain host, then a path of printable characters without spaces, '\' or ".." (file:/// alike when
// `allow_file`).
bool address(const std::string& url, bool allow_file) {
  const bool file = allow_file && url.rfind("file:///", 0) == 0;
  if (!file && url.rfind("https://", 0) != 0) return false;
  const size_t host = file ? 7 : 8, slash = url.find('/', host);
  if (!file && !plain_host(url.substr(host, slash == std::string::npos ? std::string::npos : slash - host))) return false;
  if (url.find("..") != std::string::npos) return false;
  for (size_t k = host; k < url.size(); ++k)
    if (url[k] <= ' ' || url[k] > '~' || url[k] == '\\') return false;
  return true;
}

bool plain_file_name(const std::string& n) {
  if (n.empty() || n.size() > 120 || n.front() == '.' || n.front() == ' ' || n.find("..") != std::string::npos) return false;
  for (char c : n)
    if (!std::isalnum(static_cast<unsigned char>(c)) && !std::strchr(" -_.()',", c)) return false;
  return true;
}

}  // namespace

int compare_versions(const std::string& a, const std::string& b) {
  const auto parts = [](const std::string& v, std::vector<unsigned long long>* out) {
    size_t at = 0;
    if (v.empty()) return false;
    for (;;) {
      const size_t dot = std::min(v.find('.', at), v.size());
      const std::string p = v.substr(at, dot - at);
      if (p.empty() || p.size() > 9 || p.find_first_not_of("0123456789") != std::string::npos) return false;
      out->push_back(std::strtoull(p.c_str(), nullptr, 10));
      if (dot == v.size()) return true;
      at = dot + 1;
    }
  };
  std::vector<unsigned long long> x, y;
  if (!parts(a, &x) || !parts(b, &y)) return -2;
  for (size_t n = 0; n < std::max(x.size(), y.size()); ++n) {
    const unsigned long long p = n < x.size() ? x[n] : 0, q = n < y.size() ? y[n] : 0;
    if (p != q) return p < q ? -1 : 1;
  }
  return 0;
}

std::string version_number(const std::string& tag) {
  const size_t v = tag.find_last_of("vV");
  const std::string t = v == std::string::npos ? tag : tag.substr(v + 1);
  size_t n = 0;
  while (n < t.size() && (std::isdigit(static_cast<unsigned char>(t[n])) || t[n] == '.')) ++n;
  std::string out = t.substr(0, n);
  while (!out.empty() && out.back() == '.') out.pop_back();
  return out;
}

bool read_original(const std::string& catalog_json, const std::string& key, bool allow_file, std::vector<OriginalSource>* out,
                   std::string* why, const std::string& game_tag) {
  out->clear(), why->clear();
  const std::string game_version = version_number(game_tag);
  Reader r{catalog_json};
  Json root;
  if (!r.value(&root, 0) || root.type != Json::kObject) return *why = "the catalog is not JSON", false;
  r.ws();
  if (r.i != catalog_json.size()) return *why = "the catalog is not JSON", false;
  const Json* games = root.at("games");
  if (!games || games->type != Json::kArray) return *why = "the catalog has no games list", false;
  const Json* entry = nullptr;
  for (const Json& g : games->items)
    if (const Json* k = g.at("key"); k && k->type == Json::kString && k->s == key) entry = &g;
  if (!entry) return *why = "the catalog has no entry " + key, false;
  // One list's sources onto `out`; false with `why` when the list is bad (nothing of it kept).
  const auto read_list = [&](const char* name, bool compact, std::string* list_why) {
    const Json* list = entry->at(name);
    if (!list) return true;
    const std::string label = std::string("its ") + name;
    if (list->type != Json::kArray || list->items.empty()) return *list_why = label + " is not a list of sources", false;
    if (list->items.size() > static_cast<size_t>(kMaxOriginalSources))
      return *list_why = label + " lists more than " + std::to_string(kMaxOriginalSources), false;
    std::vector<OriginalSource> got;
    for (size_t n = 0; n < list->items.size(); ++n) {
      const Json& j = list->items[n];
      const std::string where = label + "[" + std::to_string(n) + "]: ";
      OriginalSource s;
      s.compact = compact;
      std::string game_min;
      const auto text = [&](const char* k, std::string* o, bool needed) {
        const Json* v = j.at(k);
        if (!v) return !needed;
        if (v->type != Json::kString || (needed && v->s.empty())) return false;
        return *o = v->s, true;
      };
      if (j.type != Json::kObject || !text("url", &s.url, true) || !text("sha256", &s.sha256, true) || !text("name", &s.name, true) ||
          !text("note", &s.note, false) || (compact && !text("game_min", &game_min, false)))
        return *list_why = where + "not a source (url, sha256, size, name)", false;
      const Json* size = j.at("size");
      if (!size || size->type != Json::kNumber || size->s.empty() || size->s.size() > 12 || size->s.find_first_not_of("0123456789") != std::string::npos ||
          (s.size = std::strtoull(size->s.c_str(), nullptr, 10)) == 0)
        return *list_why = where + "no size in bytes", false;
      if (!address(s.url, allow_file)) return *list_why = where + s.url + " is not an https:// address", false;
      bool hex = s.sha256.size() == 64;
      for (char& c : s.sha256) hex = hex && std::isxdigit(static_cast<unsigned char>(c)), c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (!hex) return *list_why = where + "its sha256 is not 64 hex digits", false;
      if (!plain_file_name(s.name)) return *list_why = where + "its name " + s.name + " is not a plain file name", false;
      if (!game_min.empty()) {
        if (compare_versions(game_min, "0") == -2) return *list_why = where + "its game_min " + game_min + " is not a version", false;
        if (game_version.empty() || compare_versions(game_version, game_min) < 0) continue;  // a newer game's: not ours
      }
      got.push_back(std::move(s));
    }
    out->insert(out->end(), got.begin(), got.end());
    return true;
  };
  std::string bad_original, bad_compact;
  const bool original_ok = read_list("original", false, &bad_original);
  const bool compact_ok = read_list("compact", true, &bad_compact);
  if (!out->empty()) return true;
  if (!original_ok) return *why = bad_original, false;
  if (!compact_ok) return *why = bad_compact, false;
  if (!entry->at("original") && !entry->at("compact")) return *why = "the catalog lists no source of the game's files", false;
  return *why = "the catalog lists no source this version of the game reads", false;
}

std::string catalog_report(const std::string& catalog_json, const std::string& key) {
  Reader r{catalog_json};
  Json root;
  if (!r.value(&root, 0) || root.type != Json::kObject) return "";
  const Json* games = root.at("games");
  if (!games || games->type != Json::kArray) return "";
  for (const Json& g : games->items)
    if (const Json* k = g.at("key"); k && k->type == Json::kString && k->s == key) {
      const Json* rep = g.at("report");
      return rep && rep->type == Json::kString && address(rep->s, false) ? rep->s : "";
    }
  return "";
}

}  // namespace rekit
