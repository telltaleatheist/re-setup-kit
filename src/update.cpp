#include "rekit/update.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#if defined(__APPLE__)
#include <stdio.h>
#include <sys/time.h>
#endif

#include "json.h"
#include "rekit/archive.h"
#include "rekit/files.h"
#include "rekit/original.h"

namespace fs = std::filesystem;

namespace rekit {
namespace {

using detail::Json;

std::string text_of(const Json* j) { return j && j->type == Json::kString ? j->s : std::string(); }
bool true_of(const Json* j) { return j && j->type == Json::kBool && j->s == "true"; }
uint64_t number_of(const Json* j) {
  if (!j || j->type != Json::kNumber || j->s.empty() || j->s.find_first_not_of("0123456789") != std::string::npos) return 0;
  return std::strtoull(j->s.c_str(), nullptr, 10);
}

// An https:// address with a plain host and a printable path (file:/// too with allow_file), as the catalog's others.
bool good_address(const std::string& url, bool allow_file) {
  const bool file = allow_file && url.rfind("file:///", 0) == 0;
  if (!file && url.rfind("https://", 0) != 0) return false;
  const size_t from = file ? 7 : 8, slash = url.find('/', from);
  if (!file) {
    const std::string host = url.substr(from, slash == std::string::npos ? std::string::npos : slash - from);
    if (host.empty() || host.find('.') == std::string::npos || host.find("..") != std::string::npos || host.front() == '.' ||
        host.back() == '.' || host.front() == '-')
      return false;
    for (char c : host)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.') return false;
  }
  if (url.find("..") != std::string::npos) return false;
  for (size_t k = from; k < url.size(); ++k)
    if (url[k] <= ' ' || url[k] > '~' || url[k] == '\\') return false;
  return true;
}

}  // namespace

const char* install_kind_name(InstallKind k) {
  switch (k) {
    case InstallKind::kDevelopment: return "development";
    case InstallKind::kWindowsSetupUser: return "windows-setup-user";
    case InstallKind::kWindowsSetupMachine: return "windows-setup-machine";
    case InstallKind::kFolder: return "folder";
    case InstallKind::kAppImage: return "appimage";
    case InstallKind::kMacApp: return "mac-app";
    default: return "unknown";
  }
}

bool read_update_catalog(const std::string& catalog_json, bool allow_file, UpdateCatalog* out, std::string* why) {
  *out = UpdateCatalog{}, why->clear();
  Json root;
  if (!detail::parse_json(catalog_json, &root) || root.type != Json::kObject) return *why = "not a catalog (not JSON)", false;
  const Json* homes = root.at("homes");
  if (!homes || homes->type != Json::kArray) return *why = "the catalog lists no homes", false;
  for (const Json& h : homes->items) {
    ReleaseHome home;
    home.key = text_of(h.at("key")), home.releases = text_of(h.at("releases"));
    home.order = static_cast<int>(number_of(h.at("order")));
    const std::string probe = releases_address(home, "repo", 10);
    if (home.key.empty() || home.releases.find("{repo}") == std::string::npos || !good_address(probe, allow_file)) continue;
    if (static_cast<int>(out->homes.size()) < kMaxReleaseHomes) out->homes.push_back(home);
  }
  std::stable_sort(out->homes.begin(), out->homes.end(), [](const ReleaseHome& a, const ReleaseHome& b) { return a.order < b.order; });
  const std::string counts = text_of(root.at("counts"));
  if (good_address(counts, allow_file)) out->counts = counts;
  if (out->homes.empty()) return *why = "the catalog lists no usable home", false;
  return true;
}

std::string releases_address(const ReleaseHome& home, const std::string& repo, int count) {
  std::string url = home.releases;
  for (const auto& [mark, value] : {std::pair<std::string, std::string>{"{repo}", repo}, {"{count}", std::to_string(count)}})
    for (size_t at; (at = url.find(mark)) != std::string::npos;) url.replace(at, mark.size(), value);
  return url;
}

const ReleaseFile* Release::file(const std::string& name) const {
  for (const ReleaseFile& f : files)
    if (f.name == name) return &f;
  return nullptr;
}

bool parse_releases(const std::string& json, std::vector<Release>* out, std::string* why) {
  out->clear(), why->clear();
  Json root;
  if (!detail::parse_json(json, &root)) return *why = "the releases list is not JSON", false;
  if (root.type != Json::kArray) return *why = "the releases list is not a list", false;
  for (const Json& r : root.items) {
    if (r.type != Json::kObject) continue;
    Release rel;
    rel.tag = text_of(r.at("tag_name")), rel.notes = text_of(r.at("body")), rel.page = text_of(r.at("html_url"));
    rel.draft = true_of(r.at("draft")), rel.prerelease = true_of(r.at("prerelease"));
    if (const Json* assets = r.at("assets"); assets && assets->type == Json::kArray)
      for (const Json& a : assets->items) {
        ReleaseFile f{text_of(a.at("name")), text_of(a.at("browser_download_url")), number_of(a.at("size"))};
        if (!f.name.empty() && !f.url.empty()) rel.files.push_back(std::move(f));
      }
    if (!rel.tag.empty()) out->push_back(std::move(rel));
  }
  return true;
}

bool release_tag(const std::string& prefix, const std::string& tag) {
  if (tag.size() <= prefix.size() || tag.compare(0, prefix.size(), prefix) != 0) return false;
  int parts = 0;
  for (size_t i = prefix.size(); i < tag.size();) {
    const size_t from = i;
    while (i < tag.size() && std::isdigit(static_cast<unsigned char>(tag[i]))) ++i;
    if (i == from) return false;
    ++parts;
    if (i == tag.size()) break;
    if (tag[i] != '.' || ++i == tag.size()) return false;
  }
  return parts == 3;
}

std::string release_file_name(const std::string& pattern, const std::string& tag) {
  std::string s = pattern;
  for (size_t at; (at = s.find("{tag}")) != std::string::npos;) s.replace(at, 5, tag);
  return s;
}

UpdateCheck judge_update(const UpdateRecipe& recipe, const std::vector<HomeAnswer>& answers) {
  UpdateCheck c;
  const std::string current = version_number(recipe.tag);
  if (current.empty() || recipe.tag.compare(0, recipe.tag_prefix.size(), recipe.tag_prefix) != 0)
    return c.words = "This copy's version is unknown, so it cannot be checked for updates.", c;
  // Each home's usable releases, and the newest tag over all of them.
  std::vector<std::vector<Release>> lists(answers.size());
  std::string first_error;
  int read = 0;
  std::string newest;
  for (size_t h = 0; h < answers.size(); ++h) {
    const HomeAnswer& a = answers[h];
    std::string why;
    std::vector<Release> all;
    if (a.json.empty() || !parse_releases(a.json, &all, &why)) {
      if (first_error.empty()) first_error = a.json.empty() ? a.error : why;
      continue;
    }
    ++read;
    const std::string file_of = recipe.file;
    for (Release& r : all) {
      if (r.draft || r.prerelease || !release_tag(recipe.tag_prefix, r.tag) || !r.file(release_file_name(file_of, r.tag))) continue;
      if (newest.empty() || compare_versions(version_number(r.tag), version_number(newest)) > 0) newest = r.tag;
      lists[h].push_back(std::move(r));
    }
  }
  if (read == 0) return c.words = "No update could be checked for: " + (first_error.empty() ? std::string("no home to ask") : first_error) + ".", c;
  if (newest.empty()) return c.kind = UpdateCheck::kUpToDate, c.words = "No release of this game was found.", c;
  c.tag = newest;
  // Notes of every release newer than this program's, newest first, each tag once (the first home with notes for it).
  std::vector<std::pair<std::string, std::string>> notes;
  for (const auto& list : lists)
    for (const Release& r : list) {
      if (compare_versions(version_number(r.tag), current) <= 0) continue;
      const auto had = std::find_if(notes.begin(), notes.end(), [&](const auto& n) { return n.first == r.tag; });
      if (had == notes.end()) notes.emplace_back(r.tag, r.notes);
      else if (had->second.empty()) had->second = r.notes;
    }
  std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) {
    return compare_versions(version_number(a.first), version_number(b.first)) > 0;
  });
  c.notes = std::move(notes);
  // The newest as each home has it, with its file, SHA256SUMS and its signature: the download's sources, in order.
  const std::string name = release_file_name(recipe.file, newest);
  for (size_t h = 0; h < lists.size(); ++h)
    for (const Release& r : lists[h]) {
      if (r.tag != newest) continue;
      if (c.page.empty()) c.page = r.page;
      const ReleaseFile *f = r.file(name), *sums = r.file("SHA256SUMS"), *sig = r.file("SHA256SUMS.sig");
      if (f && sums && sig) c.sources.push_back({answers[h].home, *f, *sums, *sig});
    }
  const std::string plain = version_number(newest);
  if (compare_versions(plain, current) <= 0) return c.kind = UpdateCheck::kUpToDate, c.words = "Up to date (" + current + ").", c;
  c.kind = UpdateCheck::kNewer;
  c.words = c.sources.empty() ? "Version " + plain + " is out, but it carries no signed checksums, so it cannot be installed from here."
                              : "Version " + plain + " is available (this copy is " + current + ").";
  return c;
}

std::string sums_line(const std::string& sums, const std::string& name) {
  size_t at = 0;
  while (at < sums.size()) {
    const size_t nl = sums.find('\n', at);
    std::string line = sums.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    at = nl == std::string::npos ? sums.size() : nl + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 66) continue;
    std::string hash = line.substr(0, 64);
    for (char& ch : hash) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (hash.find_first_not_of("0123456789abcdef") != std::string::npos || line[64] != ' ') continue;
    std::string file = line.substr(65);
    if (!file.empty() && (file[0] == ' ' || file[0] == '*')) file.erase(0, 1);  // "  name" (text) or " *name" (binary)
    if (file == name) return hash;
  }
  return {};
}

bool swap_in(const std::string& fresh, const std::string& target, const std::string& marker, std::string* error) {
  std::error_code ec;
  const fs::path to(target), staged(target + ".new"), old(target + ".old");
  if (!fs::exists(fresh, ec)) return *error = "the new copy is not there: " + fresh, false;
  if (!fs::exists(to, ec)) return *error = "the program to replace is not there: " + target, false;
  for (const fs::path& left : {staged, old}) {  // an earlier try's leftovers, cleared only when they are ours
    const fs::file_status st = fs::symlink_status(left, ec);
    if (!fs::exists(st)) continue;
    const bool ours = fs::is_regular_file(st) || (fs::is_directory(st) && !marker.empty() && fs::exists(left / marker, ec));
    if (!ours) return *error = left.string() + " is in the way and is not this game's: move it and try again", false;
    fs::remove_all(left, ec);
  }
  fs::copy(fresh, staged, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
  if (ec) {
    const std::string why = ec.message();
    fs::remove_all(staged, ec);
    return *error = "the new copy could not be written beside the old one (" + why + ")", false;
  }
  fs::permissions(staged, fs::status(fresh, ec).permissions(), ec);  // an AppImage's execute bit
#if defined(__APPLE__)
  // A Mac app keeps its own folder and takes the new one's Contents, the two exchanged in one step: the Dock follows
  // the folder it was shown (mw3-re docs/launcher.md). Where the disk can't exchange, the whole folder moves.
  if (fs::is_directory(staged, ec) && fs::is_directory(staged / "Contents", ec) && fs::is_directory(to / "Contents", ec) &&
      renamex_np((staged / "Contents").c_str(), (to / "Contents").c_str(), RENAME_SWAP) == 0) {
    utimes(to.c_str(), nullptr);  // the Finder and the Dock read the new icon and name
    fs::remove_all(staged, ec);   // the old Contents; a failure leaves a .new, cleared by the next update
    return true;
  }
#endif
  fs::rename(to, old, ec);
  if (ec) {
    const std::string why = ec.message();
    fs::remove_all(staged, ec);
    return *error = "the old copy could not be moved aside (" + why + ")", false;
  }
  fs::rename(staged, to, ec);
  if (ec) {
    const std::string why = ec.message();
    std::error_code back;
    fs::rename(old, to, back);  // the old one back in its place
    fs::remove_all(staged, back);
    return *error = "the new copy could not be put in place (" + why + "); the old one is kept", false;
  }
  fs::remove_all(old, ec);  // a failure leaves a .old, cleared by the next update
  return true;
}

UpdatePrefs read_update_prefs(const std::string& user_folder) {
  UpdatePrefs p;
  std::ifstream f(fs::path(user_folder) / kUpdatePrefsName, std::ios::binary);
  for (std::string line; std::getline(f, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "ask_at_start=0") p.ask_at_start = false;
    else if (line.rfind("last_check=", 0) == 0) p.last_check = std::strtoll(line.c_str() + 11, nullptr, 10);
  }
  return p;
}

bool write_update_prefs(const std::string& user_folder, const UpdatePrefs& p) {
  std::error_code ec;
  fs::create_directories(user_folder, ec);
  const fs::path path = fs::path(user_folder) / kUpdatePrefsName, part = path.string() + ".part";
  {
    std::ofstream f(part, std::ios::binary | std::ios::trunc);
    f << "# Update choices (re-setup-kit): the start-up check, and when it last asked.\nask_at_start=" << (p.ask_at_start ? 1 : 0)
      << "\nlast_check=" << p.last_check << "\n";
    if (!f) return false;
  }
  fs::rename(part, path, ec);
  return !ec;
}

bool start_check_due(const UpdatePrefs& p, int64_t now) {
  return p.ask_at_start && (p.last_check > now || now - p.last_check >= 24 * 60 * 60);
}

namespace {
// A value as the site takes it, [A-Za-z0-9._-]{1,40}: anything else becomes '_' (mw3-re's query_part).
std::string query_part(const std::string& s) {
  std::string out;
  for (unsigned char c : s.substr(0, 40)) out += std::isalnum(c) || c == '-' || c == '.' || c == '_' ? static_cast<char>(c) : '_';
  return out.empty() ? std::string("unknown") : out;
}
}  // namespace

std::string update_count_url(const std::string& base, const std::string& game, const std::string& from, const std::string& to,
                             bool github, const std::string& agent) {
  return base + (base.find('?') == std::string::npos ? "?" : "&") + "kind=update&game=" + query_part(game) + "&from=" +
         query_part(from) + "&to=" + query_part(to) + "&launcher=" + query_part(agent) + "&src=" + (github ? "github" : "mirror");
}

bool unpack_release(const std::string& archive, const std::string& to, std::string* error) {
  std::error_code ec;
  if (fs::exists(fs::symlink_status(to, ec))) return *error = to + " is in the way", false;
  ArchiveReader r;
  if (!r.open_file(archive)) return *error = "the download could not be opened (" + r.error() + ")", false;
  fs::create_directories(to, ec);
  const auto fail = [&](const std::string& why) {
    fs::remove_all(to, ec);
    *error = why;
    return false;
  };
  ArchiveReader::Entry e;
  std::vector<uint8_t> buf(1 << 16);
  int files = 0;
  while (r.next(&e)) {
    if (e.other) continue;  // links and devices: a game never needs them
    const std::string rel = safe_relative(e.path);
    if (rel.empty()) {
      if (e.path.empty() || e.path == "/" || e.path == "./") continue;
      return fail("the download holds an unsafe path: " + e.path);
    }
    const fs::path out = fs::path(to) / rel;
    if (e.dir) {
      fs::create_directories(out, ec);
      continue;
    }
    fs::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    for (int64_t n; (n = r.read(buf.data(), buf.size())) != 0;) {
      if (n < 0) return fail("the download could not be read (" + r.error() + ")");
      f.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(n));
    }
    if (!f) return fail("a file could not be written: " + rel);
    f.close();
#if !defined(_WIN32)
    if (rel.find('/') == std::string::npos || rel.rfind(".so") != std::string::npos || rel.find('.') == std::string::npos)
      fs::permissions(out, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    ++files;
  }
  if (!r.error().empty()) return fail("the download could not be read (" + r.error() + ")");
  if (files == 0) return fail("the download holds no files");
  // One top folder holding everything: its contents are the game's folder.
  fs::path only;
  int entries = 0;
  for (const auto& d : fs::directory_iterator(to, ec)) ++entries, only = d.path();
  if (entries == 1 && fs::is_directory(only, ec)) {
    const fs::path lifted = fs::path(to + ".lift");
    fs::remove_all(lifted, ec);
    fs::rename(only, lifted, ec);
    if (!ec) fs::remove(to, ec), fs::rename(lifted, to, ec);
    if (ec) return fail("the download's folder could not be moved (" + ec.message() + ")");
  }
  return true;
}

namespace {
bool script_safe(const std::string& s) { return s.find('%') == std::string::npos && s.find('"') == std::string::npos; }
// The script's start: the code page set to UTF-8 (the script is written in UTF-8, so a path such as C:\Users\José
// reads as written whatever the system's OEM code page is), then wait (at most about two minutes) until the game's process is gone. tasklist lists it as
// "<name> <pid> ..."; ping stands in for a sleep, as timeout refuses to run without a console.
std::string wait_for(unsigned long pid) {
  const std::string p = std::to_string(pid);
  return "@echo off\r\nchcp 65001 >nul\r\nsetlocal\r\nset /a tries=0\r\n:wait\r\ntasklist /FI \"PID eq " + p + "\" /NH 2>nul | find \" " + p +
         " \" >nul || goto gone\r\nset /a tries+=1\r\nif %tries% GEQ 120 goto gone\r\nping -n 2 127.0.0.1 >nul\r\ngoto wait\r\n:gone\r\n";
}
std::string finish(const std::string& restart) {
  return (restart.empty() ? std::string() : "start \"\" \"" + restart + "\"\r\n") + "(goto) 2>nul & del \"%~f0\"\r\n";
}
}  // namespace

std::string windows_setup_script(const std::string& setup, bool all_users, unsigned long pid, const std::string& restart) {
  if (!script_safe(setup) || !script_safe(restart)) return {};
  return wait_for(pid) + "start \"\" /wait \"" + setup + "\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART " +
         (all_users ? "/ALLUSERS" : "/CURRENTUSER") + "\r\ndel \"" + setup + "\" >nul 2>nul\r\n" + finish(restart);
}

std::string windows_folder_script(const std::string& fresh, const std::string& target, unsigned long pid, const std::string& restart) {
  if (!script_safe(fresh) || !script_safe(target) || !script_safe(restart)) return {};
  const std::string old = target + ".old";
  return wait_for(pid) + "if exist \"" + old + "\" goto keep\r\nmove \"" + target + "\" \"" + old + "\" >nul || goto keep\r\nmove \"" +
         fresh + "\" \"" + target + "\" >nul || (move \"" + old + "\" \"" + target + "\" >nul & goto keep)\r\nrd /s /q \"" + old +
         "\"\r\n:keep\r\n" + finish(restart);
}

std::vector<std::string> posix_restart_command(unsigned long pid, const std::vector<std::string>& restart) {
  std::vector<std::string> args = {"/bin/sh", "-c",
                                   "n=0; while kill -0 " + std::to_string(pid) +
                                       " 2>/dev/null && [ $n -lt 600 ]; do n=$((n+1)); sleep 0.2; done; exec \"$0\" \"$@\""};
  args.insert(args.end(), restart.begin(), restart.end());
  return args;
}

bool write_update_note(const std::string& user_folder, const UpdateNote& note) {
  std::error_code ec;
  fs::create_directories(user_folder, ec);
  std::ofstream f(fs::path(user_folder) / kUpdateNoteName, std::ios::binary | std::ios::trunc);
  f << "# An update this game installed, reported once by its next start (re-setup-kit)\nfrom=" << note.from << "\nto=" << note.to
    << "\nsrc=" << (note.github ? "github" : "mirror") << "\ncounts=" << note.counts << "\n";
  return static_cast<bool>(f);
}

bool take_update_note(const std::string& user_folder, UpdateNote* note) {
  const fs::path p = fs::path(user_folder) / kUpdateNoteName;
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  *note = UpdateNote{};
  for (std::string line; std::getline(f, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("from=", 0) == 0) note->from = line.substr(5);
    else if (line.rfind("to=", 0) == 0) note->to = line.substr(3);
    else if (line == "src=github") note->github = true;
    else if (line.rfind("counts=https://", 0) == 0) note->counts = line.substr(7);
  }
  f.close();
  std::error_code ec;
  fs::remove(p, ec);  // taken: never reported twice, whatever comes of it
  return !note->to.empty();
}

}  // namespace rekit
