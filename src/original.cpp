#include "rekit/original.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace rekit {
namespace {

// A small JSON tree (objects in order, arrays, strings, numbers as written, true/false/null).
struct Json {
  enum Type { kNull, kBool, kNumber, kString, kArray, kObject } type = kNull;
  std::string s;  // a string's text, a number's as written
  std::vector<Json> items;
  std::vector<std::pair<std::string, Json>> fields;
  const Json* at(const char* key) const {
    for (const auto& [k, v] : fields)
      if (k == key) return &v;
    return nullptr;
  }
};

struct Reader {
  const std::string& t;
  size_t i = 0;
  void ws() {
    while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
  }
  bool str(std::string* o) {
    if (i >= t.size() || t[i] != '"') return false;
    for (++i; i < t.size() && t[i] != '"'; ++i) {
      if (t[i] != '\\') {
        *o += t[i];
        continue;
      }
      if (++i >= t.size()) return false;
      const char c = t[i];
      if (c == 'u') {  // \uXXXX: kept as '?' (no field the kit reads needs more)
        if (i + 4 >= t.size()) return false;
        i += 4, *o += '?';
      } else {
        *o += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : c;
      }
    }
    return i < t.size() && (++i, true);
  }
  bool value(Json* v, int depth) {
    if (depth > 32) return false;
    ws();
    if (i >= t.size()) return false;
    const char c = t[i];
    if (c == '{') {
      v->type = Json::kObject, ++i, ws();
      if (i < t.size() && t[i] == '}') return ++i, true;
      for (;;) {
        ws();
        std::string k;
        if (!str(&k)) return false;
        ws();
        if (i >= t.size() || t[i] != ':') return false;
        ++i;
        Json f;
        if (!value(&f, depth + 1)) return false;
        v->fields.emplace_back(std::move(k), std::move(f));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        return i < t.size() && t[i] == '}' && (++i, true);
      }
    }
    if (c == '[') {
      v->type = Json::kArray, ++i, ws();
      if (i < t.size() && t[i] == ']') return ++i, true;
      for (;;) {
        Json e;
        if (!value(&e, depth + 1)) return false;
        v->items.push_back(std::move(e));
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        return i < t.size() && t[i] == ']' && (++i, true);
      }
    }
    if (c == '"') return v->type = Json::kString, str(&v->s);
    const size_t from = i;
    while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || std::strchr("+-.", t[i]))) ++i;
    if (i == from) return false;
    v->s = t.substr(from, i - from);
    v->type = v->s == "true" || v->s == "false" ? Json::kBool : v->s == "null" ? Json::kNull : Json::kNumber;
    return true;
  }
};

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

bool read_original(const std::string& catalog_json, const std::string& key, bool allow_file, std::vector<OriginalSource>* out,
                   std::string* why) {
  out->clear(), why->clear();
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
  const Json* list = entry->at("original");
  if (!list) return *why = "the catalog lists no source of the game's files", false;
  if (list->type != Json::kArray || list->items.empty()) return *why = "its original is not a list of sources", false;
  if (list->items.size() > static_cast<size_t>(kMaxOriginalSources))
    return *why = "its original lists more than " + std::to_string(kMaxOriginalSources), false;
  for (size_t n = 0; n < list->items.size(); ++n) {
    const Json& j = list->items[n];
    const std::string where = "its original[" + std::to_string(n) + "]: ";
    OriginalSource s;
    const auto text = [&](const char* k, std::string* o, bool needed) {
      const Json* v = j.at(k);
      if (!v) return !needed;
      if (v->type != Json::kString || (needed && v->s.empty())) return false;
      return *o = v->s, true;
    };
    if (j.type != Json::kObject || !text("url", &s.url, true) || !text("sha256", &s.sha256, true) || !text("name", &s.name, true) ||
        !text("note", &s.note, false))
      return out->clear(), *why = where + "not a source (url, sha256, size, name)", false;
    const Json* size = j.at("size");
    if (!size || size->type != Json::kNumber || size->s.empty() || size->s.size() > 12 || size->s.find_first_not_of("0123456789") != std::string::npos ||
        (s.size = std::strtoull(size->s.c_str(), nullptr, 10)) == 0)
      return out->clear(), *why = where + "no size in bytes", false;
    if (!address(s.url, allow_file)) return out->clear(), *why = where + s.url + " is not an https:// address", false;
    bool hex = s.sha256.size() == 64;
    for (char& c : s.sha256) hex = hex && std::isxdigit(static_cast<unsigned char>(c)), c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!hex) return out->clear(), *why = where + "its sha256 is not 64 hex digits", false;
    if (!plain_file_name(s.name)) return out->clear(), *why = where + "its name " + s.name + " is not a plain file name", false;
    out->push_back(std::move(s));
  }
  return true;
}

}  // namespace rekit
