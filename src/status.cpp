#include "rekit/status.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace rekit {
namespace {

std::string quoted(const std::string& s) {
  std::string o = "\"";
  for (const char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c == '"' || c == '\\') o += '\\', o += ch;
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else {
      o += ch;  // UTF-8 as it is
    }
  }
  return o + "\"";
}
std::string list(const std::vector<std::string>& v) {
  std::string o = "[";
  for (size_t i = 0; i < v.size(); ++i) o += (i ? ", " : "") + quoted(v[i]);
  return o + "]";
}

// A small reader for the JSON status_json writes, and any equal JSON: objects,
// arrays, strings, numbers, true/false/null. Values are kept as text (a
// string's unescaped, anything else's as written); arrays of strings as lists.
struct Reader {
  explicit Reader(const std::string& text) : s(text) {}
  const std::string& s;
  size_t i = 0;
  std::string error;
  std::map<std::string, std::string> scalars;             // "files.state" -> "ready"
  std::map<std::string, std::vector<std::string>> lists;  // "needs" -> [...]

  void ws() {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }
  bool fail(const char* why) {
    if (error.empty()) error = std::string(why) + " at " + std::to_string(i);
    return false;
  }
  bool hex4(unsigned* out) {
    if (i + 4 > s.size()) return false;
    *out = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s[i++];
      *out = *out * 16 + (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : (std::tolower(c) - 'a' + 10));
      if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
  }
  void utf8(unsigned cp, std::string* o) {
    if (cp < 0x80) *o += static_cast<char>(cp);
    else if (cp < 0x800) *o += static_cast<char>(0xc0 | cp >> 6), *o += static_cast<char>(0x80 | (cp & 0x3f));
    else if (cp < 0x10000)
      *o += static_cast<char>(0xe0 | cp >> 12), *o += static_cast<char>(0x80 | (cp >> 6 & 0x3f)),
          *o += static_cast<char>(0x80 | (cp & 0x3f));
    else
      *o += static_cast<char>(0xf0 | cp >> 18), *o += static_cast<char>(0x80 | (cp >> 12 & 0x3f)),
          *o += static_cast<char>(0x80 | (cp >> 6 & 0x3f)), *o += static_cast<char>(0x80 | (cp & 0x3f));
  }
  bool str(std::string* o) {
    if (i >= s.size() || s[i] != '"') return fail("a string expected");
    ++i;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') {
        *o += c;
        continue;
      }
      if (i >= s.size()) return fail("a broken escape");
      c = s[i++];
      switch (c) {
        case 'n': *o += '\n'; break;
        case 'r': *o += '\r'; break;
        case 't': *o += '\t'; break;
        case 'b': *o += '\b'; break;
        case 'f': *o += '\f'; break;
        case 'u': {
          unsigned cp;
          if (!hex4(&cp)) return fail("a broken \\u");
          if (cp >= 0xd800 && cp < 0xdc00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            i += 2;
            unsigned lo;
            if (!hex4(&lo)) return fail("a broken \\u");
            cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
          }
          utf8(cp, o);
          break;
        }
        default: *o += c;
      }
    }
    if (i >= s.size()) return fail("an unended string");
    ++i;
    return true;
  }
  bool value(const std::string& path, int depth) {
    if (depth > 16) return fail("too deep");
    ws();
    if (i >= s.size()) return fail("a value expected");
    if (s[i] == '{') {
      ++i;
      ws();
      if (i < s.size() && s[i] == '}') return ++i, true;
      for (;;) {
        ws();
        std::string key;
        if (!str(&key)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return fail("':' expected");
        ++i;
        if (!value(path.empty() ? key : path + "." + key, depth + 1)) return false;
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') return ++i, true;
        return fail("',' or '}' expected");
      }
    }
    if (s[i] == '[') {
      ++i;
      std::vector<std::string>& l = lists[path];
      ws();
      if (i < s.size() && s[i] == ']') return ++i, true;
      for (;;) {
        ws();
        if (i < s.size() && s[i] == '"') {
          std::string v;
          if (!str(&v)) return false;
          l.push_back(v);
        } else if (!value(path + "[]", depth + 1)) {
          return false;
        }
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') return ++i, true;
        return fail("',' or ']' expected");
      }
    }
    if (s[i] == '"') {
      std::string v;
      if (!str(&v)) return false;
      scalars[path] = v;
      return true;
    }
    const size_t from = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.')) ++i;
    if (i == from) return fail("a value expected");
    scalars[path] = s.substr(from, i - from);
    return true;
  }
};

}  // namespace

std::string state_name(Status::State s) {
  return s == Status::kReady ? "ready" : s == Status::kIncomplete ? "incomplete" : "missing";
}

std::string status_json(const Status& s) {
  std::string o = "{\"kit\": " + std::to_string(kStatusVersion) + ", \"game\": " + quoted(s.game) +
                  ", \"version\": " + quoted(s.version) + ", \"files\": {\"state\": " + quoted(state_name(s.state));
  if (!s.where.empty()) o += ", \"where\": " + quoted(s.where);
  if (!s.data.empty()) o += ", \"data\": " + quoted(s.data);
  if (!s.missing.empty()) o += ", \"missing\": " + list(s.missing);
  o += "}, \"first_run\": {\"pending\": " + list(s.pending) + "}, \"needs\": " + list(s.needs);
  if (!s.note.empty()) o += ", \"note\": " + quoted(s.note);
  if (s.setup) o += ", \"setup\": " + std::to_string(s.setup);
  return o + "}";
}

std::string setup_message_json(const SetupMessage& m) {
  std::string o = std::string("{\"done\": ") + (m.done ? "true" : "false");
  if (!m.done) {
    char f[32];
    std::snprintf(f, sizeof f, "%d", static_cast<int>(m.fraction * 1000000.0));  // whole millionths: no locale's comma
    return o + ", \"at\": " + f + ", \"stage\": " + quoted(m.stage) + ", \"message\": " + quoted(m.message) + "}";
  }
  o += std::string(", \"ok\": ") + (m.ok ? "true" : "false");
  if (m.cancelled) o += ", \"cancelled\": true";
  if (m.in_place) o += ", \"in_place\": true";
  for (const auto& [key, v] : {std::pair<const char*, const std::string*>{"path", &m.path}, {"words", &m.words}, {"into", &m.into},
                               {"where", &m.where}, {"summary", &m.summary}, {"title", &m.title}, {"pick_words", &m.pick_words},
                               {"find_sub", &m.find_sub}, {"find_words", &m.find_words}})
    if (!v->empty()) o += std::string(", \"") + key + "\": " + quoted(*v);
  return o + "}";
}

bool parse_setup_message(const std::string& json, SetupMessage* out) {
  Reader r(json);
  r.ws();
  if (r.i >= json.size() || json[r.i] != '{' || !r.value("", 0)) return false;
  r.ws();
  if (r.i != json.size() || !r.scalars.count("done")) return false;
  SetupMessage m;
  m.done = r.scalars["done"] == "true", m.ok = r.scalars["ok"] == "true", m.cancelled = r.scalars["cancelled"] == "true";
  m.in_place = r.scalars["in_place"] == "true";
  m.fraction = std::atoi(r.scalars["at"].c_str()) / 1000000.0;
  m.stage = r.scalars["stage"], m.message = r.scalars["message"], m.path = r.scalars["path"], m.words = r.scalars["words"];
  m.into = r.scalars["into"], m.where = r.scalars["where"], m.summary = r.scalars["summary"], m.title = r.scalars["title"];
  m.pick_words = r.scalars["pick_words"], m.find_sub = r.scalars["find_sub"], m.find_words = r.scalars["find_words"];
  *out = m;
  return true;
}

bool parse_status(const std::string& json, Status* out, std::string* error) {
  Reader r(json);
  auto bad = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  r.ws();
  if (r.i >= json.size() || json[r.i] != '{') return bad("not a JSON object");
  if (!r.value("", 0)) return bad(r.error);
  r.ws();
  if (r.i != json.size()) return bad("more after the object");
  auto get = [&](const char* k) {
    const auto it = r.scalars.find(k);
    return it == r.scalars.end() ? std::string() : it->second;
  };
  const std::string kit = get("kit");
  if (kit.empty()) return bad("no \"kit\"");
  if (std::atoi(kit.c_str()) > kStatusVersion) return bad("a newer kit (" + kit + ")");
  Status s;
  s.game = get("game"), s.version = get("version"), s.where = get("files.where"), s.data = get("files.data");
  s.note = get("note"), s.setup = std::atoi(get("setup").c_str());
  const std::string st = get("files.state");
  s.state = st == "ready" ? Status::kReady : st == "incomplete" ? Status::kIncomplete : Status::kMissing;
  s.missing = r.lists["files.missing"], s.pending = r.lists["first_run.pending"], s.needs = r.lists["needs"];
  *out = s;
  return true;
}

}  // namespace rekit
