#pragma once
// The kit's small JSON reader (internal): a tree of objects (fields in order), arrays, strings (\\u escapes as UTF-8),
// numbers as written, true/false/null. Catalogs and releases lists are read with it.
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace rekit::detail {

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
  // The four hex digits after "\\u" (i on the 'u'), i left on the last; false when they are not.
  bool hex4(uint32_t* cp) {
    if (i + 4 >= t.size()) return false;
    *cp = 0;
    for (int k = 1; k <= 4; ++k) {
      const char h = t[i + k];
      const int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
      if (d < 0) return false;
      *cp = *cp * 16 + static_cast<uint32_t>(d);
    }
    i += 4;
    return true;
  }
  static void utf8(uint32_t cp, std::string* o) {
    if (cp < 0x80) {
      *o += static_cast<char>(cp);
    } else if (cp < 0x800) {
      *o += static_cast<char>(0xC0 | (cp >> 6)), *o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      *o += static_cast<char>(0xE0 | (cp >> 12)), *o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      *o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      *o += static_cast<char>(0xF0 | (cp >> 18)), *o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      *o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), *o += static_cast<char>(0x80 | (cp & 0x3F));
    }
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
      if (c == 'u') {  // \uXXXX as UTF-8, a surrogate pair as one character (a release's notes need them)
        uint32_t cp = 0;
        if (!hex4(&cp)) return false;
        if (cp >= 0xD800 && cp < 0xDC00 && i + 2 < t.size() && t[i + 1] == '\\' && t[i + 2] == 'u') {
          const size_t keep = i;
          uint32_t lo = 0;
          i += 2;
          if (hex4(&lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          else i = keep;
        }
        if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;  // a lone surrogate
        utf8(cp, o);
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

// `text` read whole into `out`: false when it is not one JSON value (with only spaces after it).
inline bool parse_json(const std::string& text, Json* out) {
  Reader r{text};
  if (!r.value(out, 0)) return false;
  r.ws();
  return r.i == text.size();
}

}  // namespace rekit::detail
