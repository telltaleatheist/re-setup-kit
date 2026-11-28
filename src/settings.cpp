#include "rekit/settings.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>

#include "json.h"

namespace rekit {
namespace fs = std::filesystem;

namespace {
std::mutex g_m;
KitSettings g_s;
bool g_loaded = false;
std::string g_program, g_user;
std::function<void(const std::string&)> g_log;

std::string num(double v) {
  std::ostringstream o;
  o << v;
  return o.str();
}

std::string read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return f ? std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()) : std::string();
}

// A JSON tree written back as text (the live layer keeps the catalog's objects as they were).
void write_json(const detail::Json& j, std::string* o) {
  switch (j.type) {
    case detail::Json::kNull: *o += "null"; break;
    case detail::Json::kBool:
    case detail::Json::kNumber: *o += j.s; break;
    case detail::Json::kString:
      *o += '"';
      for (const char c : j.s) {
        if (c == '"' || c == '\\') *o += '\\', *o += c;
        else if (static_cast<unsigned char>(c) < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          *o += b;
        } else *o += c;
      }
      *o += '"';
      break;
    case detail::Json::kArray:
      *o += '[';
      for (size_t i = 0; i < j.items.size(); ++i) {
        if (i) *o += ',';
        write_json(j.items[i], o);
      }
      *o += ']';
      break;
    case detail::Json::kObject:
      *o += '{';
      for (size_t i = 0; i < j.fields.size(); ++i) {
        if (i) *o += ',';
        detail::Json k;
        k.type = detail::Json::kString, k.s = j.fields[i].first;
        write_json(k, o), *o += ':', write_json(j.fields[i].second, o);
      }
      *o += '}';
      break;
  }
}

constexpr const char* kLiveFile = "setup-kit-live.json";

// Every layer read again over the defaults (g_m held). The live file: {"read": <unix time>, "layers": [{...}, ...]}.
void read_layers() {
  auto say = [](const std::string& l) {
    if (g_log) g_log("settings: " + l);
  };
  KitSettings s;
  auto file = [&](const std::string& dir) {
    if (dir.empty()) return;
    const fs::path p = fs::path(dir) / kKitSettingsFile;
    const std::string text = read_file(p);
    if (text.empty()) return;
    say("read " + p.string());
    s = read_kit_settings(text, s, [&](const std::string& l) { say(p.string() + ": " + l); });
  };
  file(g_program);
  if (!g_user.empty()) {
    detail::Json live;
    if (detail::parse_json(read_file(fs::path(g_user) / kLiveFile), &live) && live.type == detail::Json::kObject)
      if (const detail::Json* layers = live.at("layers"); layers && layers->type == detail::Json::kArray)
        for (const detail::Json& l : layers->items) {
          std::string text;
          write_json(l, &text);
          s = read_kit_settings(text, s, [&](const std::string& m) { say("the catalog's values: " + m); });
        }
  }
  if (g_user != g_program) file(g_user);
  g_s = s;
}
}  // namespace

KitSettings kit_settings() {
  std::lock_guard<std::mutex> lock(g_m);
  return g_s;
}

KitSettings read_kit_settings(const std::string& text, const KitSettings& base,
                              const std::function<void(const std::string&)>& note) {
  KitSettings s = base;
  auto say = [&](const std::string& l) {
    if (note) note(l);
  };
  detail::Json j;
  if (!detail::parse_json(text, &j) || j.type != detail::Json::kObject) {
    say("not a JSON object: every value kept");
    return s;
  }
  auto number = [&](const char* key, double lo, double hi, double* v) {
    const detail::Json* f = j.at(key);
    if (!f) return;
    char* end = nullptr;
    const double d = f->type == detail::Json::kNumber ? std::strtod(f->s.c_str(), &end) : NAN;
    if (f->type != detail::Json::kNumber || !end || *end || !std::isfinite(d)) {
      say(std::string(key) + " is not a number: " + num(*v) + " kept");
    } else if (d < lo) {
      say(std::string(key) + " " + f->s + " is below " + num(lo) + ": " + num(lo) + " used"), *v = lo;
    } else if (d > hi) {
      say(std::string(key) + " " + f->s + " is above " + num(hi) + ": " + num(hi) + " used"), *v = hi;
    } else {
      *v = d;
    }
  };
  auto whole = [&](const char* key, int lo, int hi, int* v) {
    double d = *v;
    number(key, lo, hi, &d);
    *v = static_cast<int>(std::lround(d));
  };
  whole("hint_starts", 0, 20, &s.hint_starts);
  number("hint_seconds", 3, 60, &s.hint_seconds);
  number("hint_fade_in_seconds", 0, 2, &s.hint_fade_in_seconds);
  number("hint_fade_out_seconds", 0, 3, &s.hint_fade_out_seconds);
  number("update_notice_seconds", 3, 60, &s.update_notice_seconds);
  whole("check_listed", 1, 50, &s.check_listed);
  number("click_volume", 0, 1, &s.click_volume);
  number("hover_volume", 0, 1, &s.hover_volume);
  whole("update_check_hours", 12, 720, &s.update_check_hours);
  whole("catalog_seconds", 5, 120, &s.catalog_seconds);
  whole("releases_seconds", 5, 120, &s.releases_seconds);
  whole("releases_count", 10, 40, &s.releases_count);
  whole("sums_seconds", 10, 300, &s.sums_seconds);
  whole("count_seconds", 3, 60, &s.count_seconds);
  whole("download_connect_seconds", 5, 120, &s.download_connect_seconds);
  whole("download_slow_bytes_per_second", 1, 65536, &s.download_slow_bytes_per_second);
  whole("download_slow_seconds", 15, 600, &s.download_slow_seconds);
  whole("live_every_hours", 1, 168, &s.live_every_hours);
  auto https = [](const std::string& u) {
    return u.rfind("https://", 0) == 0 && u.size() > 8 && u.size() < 2048 && u.find_first_of(" \t\r\n\"<>") == std::string::npos;
  };
  if (const detail::Json* f = j.at("catalogs")) {
    std::vector<std::string> list;
    bool ok = f->type == detail::Json::kArray && f->items.size() <= 5;
    for (const detail::Json& u : ok ? f->items : std::vector<detail::Json>{})
      if (u.type == detail::Json::kString && https(u.s)) list.push_back(u.s);
      else ok = false;
    if (ok) s.catalogs = list;
    else say("catalogs is not a list of up to 5 https:// addresses: the list kept");
  }
  if (const detail::Json* f = j.at("report_url")) {
    const bool ok = f->type == detail::Json::kString && (f->s.empty() || https(f->s));
    if (ok) s.report_url = f->s;
    else say("report_url is not an https:// address: " + (s.report_url.empty() ? std::string("the game's") : s.report_url) + " kept");
  }
  return s;
}

void load_kit_settings(const std::string& program_dir, const std::string& user_folder,
                       const std::function<void(const std::string&)>& log) {
  std::lock_guard<std::mutex> lock(g_m);
  if (g_loaded || program_dir.empty()) return;
  g_loaded = true, g_program = program_dir, g_user = user_folder, g_log = log;
  read_layers();
}

bool apply_live_kit_settings(const std::string& catalog_json, const std::string& key, const std::string& user_folder,
                             const std::function<void(const std::string&)>& log) {
  detail::Json root;
  if (user_folder.empty() || !detail::parse_json(catalog_json, &root) || root.type != detail::Json::kObject) return false;
  std::string layers;
  auto add = [&](const detail::Json& o) {
    if (o.type != detail::Json::kObject) return;
    if (!layers.empty()) layers += ',';
    write_json(o, &layers);
  };
  if (const detail::Json* all = root.at("setup_kit")) add(*all);
  if (const detail::Json* games = root.at("games"); games && games->type == detail::Json::kArray)
    for (const detail::Json& g : games->items)
      if (const detail::Json* k = g.at("key"); k && k->type == detail::Json::kString && k->s == key) {
        if (const detail::Json* rep = g.at("report"); rep && rep->type == detail::Json::kString) {
          detail::Json o;
          o.type = detail::Json::kObject, o.fields.push_back({"report_url", *rep});
          add(o);
        }
        if (const detail::Json* own = g.at("setup_kit")) add(*own);
      }
  const std::string text = "{\"read\":" + std::to_string(static_cast<long long>(std::time(nullptr))) + ",\"layers\":[" + layers + "]}\n";
  std::error_code ec;
  const fs::path to = fs::path(user_folder) / kLiveFile, part = to.string() + ".part";
  {
    std::ofstream f(part, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) return false;
  }
  fs::rename(part, to, ec);
  if (ec) return false;
  if (log) log("settings: the catalog's values kept in " + to.string());
  std::lock_guard<std::mutex> lock(g_m);
  if (g_loaded && g_user == user_folder) read_layers();
  return !layers.empty();
}

bool live_kit_settings_due(const std::string& user_folder, int64_t now) {
  detail::Json live;
  if (user_folder.empty() || !detail::parse_json(read_file(fs::path(user_folder) / kLiveFile), &live)) return true;
  const detail::Json* read = live.at("read");
  if (!read || read->type != detail::Json::kNumber) return true;
  const int64_t t = std::strtoll(read->s.c_str(), nullptr, 10);
  return t > now || now - t >= static_cast<int64_t>(kit_settings().live_every_hours) * 60 * 60;
}

void reset_kit_settings(const KitSettings& s) {
  std::lock_guard<std::mutex> lock(g_m);
  g_s = s, g_loaded = false, g_program.clear(), g_user.clear(), g_log = nullptr;
}

}  // namespace rekit
