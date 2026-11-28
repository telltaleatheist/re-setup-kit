#include "rekit/sdl/update.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#include "rekit/backup.h"
#include "rekit/hash.h"
#include "rekit/original.h"
#include "rekit/program_record.h"
#include "rekit/sdl/setup_original.h"
#include "rekit/settings.h"
#include "rekit/sign.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

unsigned long own_pid() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return static_cast<unsigned long>(getpid());
#endif
}

// `args` started on their own, not waited for (they outlive this program); on Windows with no console window. `in`:
// its working folder ("" this program's).
bool start_detached(const std::vector<std::string>& args, const std::string& in = "") {
  std::vector<const char*> argv;
  for (const std::string& a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
  if (!in.empty()) SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, in.c_str());
#if defined(_WIN32)
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
  SDL_Process* p = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (!p) return false;
  SDL_DestroyProcess(p);
  return true;
}

#if defined(__APPLE__)
// `args` run and waited for: its exit code (-1 not started), its output in `out`.
int run_wait(const std::vector<std::string>& args, std::string* out) {
  std::vector<const char*> argv;
  for (const std::string& a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  SDL_Process* p = SDL_CreateProcess(argv.data(), true);
  if (!p) return -1;
  size_t n = 0;
  void* data = SDL_ReadProcess(p, &n, nullptr);
  if (data && out) out->assign(static_cast<const char*>(data), n);
  SDL_free(data);
  int code = -1;
  SDL_WaitProcess(p, true, &code);
  SDL_DestroyProcess(p);
  return code;
}
#endif

// The .app holding `program` ("" none).
fs::path app_of(const fs::path& program) {
  for (fs::path p = program; p.has_parent_path() && p != p.parent_path(); p = p.parent_path())
    if (p.extension() == ".app") return p;
  return {};
}

int64_t now_of(const UpdateSource& src) {
  return src.now_s ? src.now_s() : static_cast<int64_t>(std::time(nullptr));
}

#if defined(_WIN32)
// A path as UTF-8, as SDL's process calls and the helper script take it (path::string() is the program's code page).
std::string u8(const fs::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}
#endif

constexpr const char* kMoveWords =
    "This copy runs from a place it can't replace itself in (Downloads or its disk image). Move it to Applications, "
    "start it from there, and update again.";

}  // namespace

bool launched_by_launcher() {
  const char* v = SDL_getenv("RE_LAUNCHER");
  return v && *v;
}

std::string update_dir(const std::string& temp, const std::string& key, std::string* error) {
  std::error_code ec;
  const fs::path base = temp.empty() ? fs::temp_directory_path(ec) : fs::path(temp);
  if (base.empty()) return *error = "this computer has no temp folder", std::string();
  const fs::path dir = base / ("rekit-update-" + key);
  if (fs::exists(dir, ec) && !fs::exists(dir / kUpdateMark, ec) && !fs::is_empty(dir, ec))
    return *error = dir.string() + " is not the kit's folder, so nothing was written to it", std::string();
  fs::create_directories(dir, ec);
  std::ofstream(dir / kUpdateMark) << "re-setup-kit: a game's update while it downloads and installs.\n";
  if (!fs::exists(dir / kUpdateMark, ec)) return *error = dir.string() + " could not be made", std::string();
  return dir.string();
}

UpdateCheck check_update(const UpdateSource& src, std::string* counts, const std::function<bool()>& go_on) {
  UpdateCheck c;
  counts->clear();
  if (!src.usable()) return c.words = "This game has no update source.", c;
  std::string error;
  const std::string dir = update_dir(src.temp, src.recipe.key, &error);
  if (dir.empty()) return c.words = "No update could be checked for: " + error + ".", c;
  const auto go = [&](double) { return !go_on || go_on(); };
  std::string text, why;
  if (!fetch_signed_catalog(src.catalogs, src.keys, dir, src.allow_file, go, &text, &why))
    return c.words = "No update could be checked for: " + why + ".", c;
  UpdateCatalog cat;
  if (!read_update_catalog(text, src.allow_file, &cat, &why)) return c.words = "No update could be checked for: " + why + ".", c;
  *counts = cat.counts;
  std::vector<HomeAnswer> answers;
  for (const ReleaseHome& h : cat.homes) {
    if (go_on && !go_on()) break;
    HomeAnswer a{h.key, "", ""};
    const fs::path list = fs::path(dir) / ("releases-" + h.key + ".json");
    bool cancelled = false;
    if (fetch_url(releases_address(h, src.recipe.repo, kit_settings().releases_count), list.string(), 0, kit_settings().releases_seconds, src.allow_file, go, &a.error, &cancelled, true))
      a.json = read_text(list);
    std::error_code ec;
    fs::remove(list, ec);
    answers.push_back(std::move(a));
    if (cancelled) break;
  }
  return judge_update(src.recipe, answers);
}

bool download_update(const UpdateSource& src, const UpdateCheck& check, const std::string& dir, const ProgressFn& progress,
                     std::string* file, bool* github, std::string* error, bool* cancelled) {
  *cancelled = false, file->clear(), *github = false;
  std::string reasons;
  std::error_code ec;
  for (const UpdateCheck::Source& s : check.sources) {
    const auto bar = [&](double f) {
      Progress p;
      p.stage = "Downloading the update", p.fraction = f, p.message = s.file.name;
      return !progress || progress(p);
    };
    const fs::path sums = fs::path(dir) / "SHA256SUMS", sig = fs::path(dir) / "SHA256SUMS.sig", to = fs::path(dir) / s.file.name;
    std::string bad, why;
    if (!fetch_url(s.sums.url, sums.string(), 0, kit_settings().sums_seconds, src.allow_file, bar, &why, cancelled, true)) {
      bad = "no checksums (" + why + ")";
    } else if (!fetch_url(s.sig.url, sig.string(), 0, kit_settings().sums_seconds, src.allow_file, bar, &why, cancelled, true)) {
      bad = "no checksums' signature (" + why + ")";
    } else if (rekit::signed_by(read_text(sums), read_text(sig), src.keys).empty()) {
      bad = "its checksums' signature does not check against the game's keys";
    } else if (const std::string want = sums_line(read_text(sums), s.file.name); want.empty()) {
      bad = "its checksums do not list " + s.file.name;
    } else if (!fetch_url(s.file.url, to.string(), s.file.size, 0, src.allow_file, bar, &why, cancelled)) {
      bad = why;
    } else if (s.file.size && fs::file_size(to, ec) != s.file.size) {
      bad = "it came short";
    } else if (sha256_file_hex(to.string()) != want) {
      bad = "it does not match its checksum";
    } else {
      fs::remove(sums, ec), fs::remove(sig, ec);
      *file = to.string(), *github = s.home == "github";
      return true;
    }
    fs::remove(sums, ec), fs::remove(sig, ec), fs::remove(to, ec);
    if (*cancelled) return *error = "The download was canceled.", false;
    reasons += (reasons.empty() ? "" : "; ") + s.home + ": " + bad;
  }
  *error = reasons.empty() ? "no signed copy of the update to download" : reasons;
  return false;
}

namespace {
// What an update replaces and the path inside it that marks it the game's; "" when not found.
fs::path update_target(const UpdateSource& src, InstallKind kind, const std::string& self, std::string* marker) {
  fs::path target = src.target;
  *marker = src.marker;
  if (kind == InstallKind::kMacApp) {
    if (target.empty()) target = app_of(self);
    if (!target.empty()) *marker = fs::path(self).lexically_relative(target).generic_string();
  } else if (kind == InstallKind::kAppImage) {
    if (target.empty()) target = self;
  } else if (kind == InstallKind::kFolder) {
    if (target.empty()) target = fs::path(self).parent_path();
    if (marker->empty()) *marker = fs::path(self).lexically_relative(target).generic_string();
  }
  return target;
}
// This user can make a file in `dir` (where the swap writes <target>.new and <target>.old): a file made and removed.
bool writable(const fs::path& dir) {
  std::error_code ec;
  const fs::path probe = dir / ".rekit-update-probe";
  { std::ofstream f(probe, std::ios::binary); if (!f) return false; }
  fs::remove(probe, ec);
  return true;
}
}  // namespace

std::string update_blocked(const UpdateSource& src, InstallKind kind) {
  if (kind == InstallKind::kUnknown || kind == InstallKind::kDevelopment)
    return "This copy can't update itself: get the new version from its release page.";
  const std::string self = self_path();
  if (self.empty()) return kMoveWords;  // a Mac app run translocated
  if (launchers_own(self)) return "The launcher installed this copy: update it from the launcher.";
  if (kind == InstallKind::kWindowsSetupUser || kind == InstallKind::kWindowsSetupMachine) return {};  // the Setup finds it
  std::string marker;
  const fs::path target = update_target(src, kind, self, &marker);
  if (target.empty()) return "This copy is not inside a Mac app, so it can't update itself.";
  if (!writable(target.parent_path()))
    return "This copy's folder (" + target.parent_path().string() +
           ") can't be changed by this user, so it can't update itself: get the new version from its release page.";
  return {};
}

UpdateInstall install_update(const UpdateSource& src, InstallKind kind, const std::string& user_folder, const std::string& file,
                             bool github, const std::string& to, const std::string& counts) {
  UpdateInstall r;
  std::error_code ec;
  const std::string self = self_path();
  if (const std::string why = update_blocked(src, kind); !why.empty()) return r.words = why, r;
  std::string marker;
  const fs::path target = update_target(src, kind, self, &marker);
  // The player's files first: nothing changes when they could not be copied.
  const std::vector<std::string> keep = src.keep_now ? src.keep_now() : src.keep;
  if (!keep.empty()) {
    const Backup b = back_up(user_folder, keep, "update", kBackupsKept);
    if (!b.ok) return r.words = "Your files could not be backed up, so nothing was changed: " + b.error, r;
    r.backup = b.folder;
  }
  UpdateNote note{version_number(src.recipe.tag), version_number(to), counts, github};
  const auto noted = [&]() { write_update_note(user_folder, note); };
  const std::string dir = fs::path(file).parent_path().string();
  std::vector<std::string> restart;
  switch (kind) {
#if defined(_WIN32)
    case InstallKind::kWindowsSetupUser:
    case InstallKind::kWindowsSetupMachine:
    case InstallKind::kFolder: {
      std::string script;
      if (kind == InstallKind::kFolder) {
        const fs::path fresh = target.string() + ".new";  // beside it: a folder moves only on its own drive
        if (fs::exists(fresh, ec)) {
          if (marker.empty() || !fs::exists(fresh / marker, ec)) return r.words = fresh.string() + " is in the way and is not this game's.", r;
          fs::remove_all(fresh, ec);
        }
        std::string why;
        if (!unpack_release(file, fresh.string(), &why)) return r.words = "The update could not be unpacked: " + why, r;
        fs::remove(file, ec);
        script = windows_folder_script(u8(fresh), u8(target), own_pid(), src.no_restart ? "" : u8(fs::path(self)));
      } else {
        script = windows_setup_script(u8(fs::path(file)), kind == InstallKind::kWindowsSetupMachine, own_pid(),
                                      src.no_restart ? "" : u8(fs::path(self)));
      }
      if (script.empty()) return r.words = "This copy's path has a character the updater can't carry (% or \").", r;
      const fs::path cmd_file = fs::path(dir) / "rekit-update.cmd";
      std::ofstream(cmd_file, std::ios::binary | std::ios::trunc) << script;
      const char* root = SDL_getenv("SystemRoot");
      const std::string cmd = std::string(root && *root ? root : "C:\\Windows") + "\\System32\\cmd.exe";  // SDL's UTF-8
      noted();
      if (!start_detached({cmd, "/d", "/c", u8(cmd_file)}, dir)) {  // never in the folder it swaps (the script cds out too)
        fs::remove(fs::path(user_folder) / kUpdateNoteName, ec);
        return r.words = "The updater could not be started.", r;
      }
      r.quit = true;
      r.words = kind == InstallKind::kWindowsSetupMachine ? "The game closes now; Windows asks to allow the update, then the game starts again."
                                                          : "The game closes now, updates and starts again.";
      return r;
    }
#else
    case InstallKind::kAppImage: {
      fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
      std::string why;
      if (!swap_in(file, target.string(), "", &why)) return r.words = "The update could not be put in place: " + why, r;
      fs::remove(file, ec);
      restart = {target.string()};
      break;
    }
    case InstallKind::kFolder: {
      const fs::path fresh = fs::path(dir) / "unpacked";
      fs::remove_all(fresh, ec);  // in the kit's own marked folder
      std::string why;
      if (!unpack_release(file, fresh.string(), &why)) return r.words = "The update could not be unpacked: " + why, r;
      fs::remove(file, ec);
      const bool ok = swap_in(fresh.string(), target.string(), marker, &why);
      fs::remove_all(fresh, ec);
      if (!ok) return r.words = "The update could not be put in place: " + why, r;
      restart = {self};
      break;
    }
#endif
#if defined(__APPLE__)
    case InstallKind::kMacApp: {
      const fs::path mount = fs::path(dir) / "mount";
      fs::create_directories(mount, ec);
      std::string said;
      if (run_wait({"/usr/bin/hdiutil", "attach", "-nobrowse", "-readonly", "-noautoopen", "-mountpoint", mount.string(), file}, &said) != 0)
        return fs::remove(mount, ec), r.words = "The update's disk image could not be opened.", r;
      fs::path app;
      for (const auto& e : fs::directory_iterator(mount, ec))
        if (e.path().extension() == ".app" && !fs::is_symlink(e.symlink_status(ec))) app = e.path();
      std::string why = "the disk image holds no app";
      const bool ok = !app.empty() && swap_in(app.string(), target.string(), marker, &why);
      run_wait({"/usr/bin/hdiutil", "detach", "-force", mount.string()}, nullptr);
      fs::remove(mount, ec);
      if (!ok) return r.words = "The update could not be put in place: " + why, r;
      fs::remove(file, ec);
      restart = {"/usr/bin/open", "-n", target.string()};
      if (!src.restart_args.empty()) restart.push_back("--args");
      break;
    }
#endif
    default:
      return r.words = "This copy can't update itself on this system: get the new version from its release page.", r;
  }
#if !defined(_WIN32)
  // Swapped: the note for the next start, then a helper that starts the game again once this one has gone.
  noted();
  restart.insert(restart.end(), src.restart_args.begin(), src.restart_args.end());
  if (!src.no_restart && !start_detached(posix_restart_command(own_pid(), restart)))
    return r.quit = true, r.words = "The game is updated; start it again.", r;
  r.quit = true, r.words = "The game closes now and starts again updated.";
#endif
  return r;
}

std::string report_finished_update(const UpdateSource& src, const std::string& user_folder, const std::function<bool()>& go_on,
                                   std::string* finished) {
  UpdateNote n;
  if (!take_update_note(user_folder, &n)) return {};
  const int got_there = compare_versions(n.to, version_number(src.recipe.tag));
  if (got_there > 0) return n.to;  // still the older program: the update did not finish (not counted)
  if (got_there == 0 && finished) *finished = n.from + " to " + n.to + (n.github ? " (github)" : " (mirror)");
  if (got_there != 0 || n.counts.empty() || kit_test_mode()) return {};
  std::string error;
  const std::string dir = update_dir(src.temp, src.recipe.key, &error);
  if (dir.empty()) return {};
  const fs::path got = fs::path(dir) / "count.txt";
  bool cancelled = false;
  fetch_url(update_count_url(n.counts, src.recipe.key, n.from, n.to, n.github, kCountAgent), got.string(), 0, kit_settings().count_seconds, src.allow_file,
            [&](double) { return !go_on || go_on(); }, &error, &cancelled, true);
  std::error_code ec;
  fs::remove(got, ec);
  return {};
}

struct StartUpdateCheck::State {
  std::mutex m;
  std::atomic<bool> stop{false}, done{false};
  bool taken = false;
  UpdateCheck check;
  std::string counts, unfinished, finished;
  std::thread thread;
};

StartUpdateCheck::StartUpdateCheck(UpdateSource src, std::string user_folder) : state_(std::make_shared<State>()) {
  if (const char* base = SDL_GetBasePath()) load_kit_settings(base, user_folder, nullptr);  // the game's menu may read them first
  if (const KitSettings ks = kit_settings(); !ks.catalogs.empty() && !src.allow_file) src.catalogs = ks.catalogs;
  if (!src.usable() || user_folder.empty()) {
    state_->done = true;
    return;
  }
  std::shared_ptr<State> s = state_;
  s->thread = std::thread([s, src = std::move(src), user_folder = std::move(user_folder)]() {
    std::string finished;
    std::string unfinished = report_finished_update(src, user_folder, [s]() { return !s->stop.load(); }, &finished);
    {
      std::lock_guard<std::mutex> lock(s->m);
      s->unfinished = unfinished, s->finished = finished;
    }
    UpdatePrefs prefs = read_update_prefs(user_folder);
    const int64_t now = now_of(src);
    const bool due = !unfinished.empty() || (!launched_by_launcher() && start_check_due(prefs, now));
    if (!s->stop && !kit_test_mode() && due) {
      prefs.last_check = now;
      write_update_prefs(user_folder, prefs);  // once a day, whatever the answer
      std::string counts;
      UpdateCheck c = check_update(src, &counts, [s]() { return !s->stop.load(); });
      std::lock_guard<std::mutex> lock(s->m);
      s->check = std::move(c), s->counts = std::move(counts);
    }
    s->done = true;
  });
}

StartUpdateCheck::~StartUpdateCheck() {
  state_->stop = true;  // a fetch in progress is stopped (fetch_url asks go_on as it waits)
  if (state_->thread.joinable()) state_->thread.join();
}

bool StartUpdateCheck::running() const { return !state_->done; }

bool StartUpdateCheck::take(UpdateCheck* check, std::string* counts, std::string* unfinished, std::string* finished) {
  if (!state_->done) return false;
  std::lock_guard<std::mutex> lock(state_->m);
  if (state_->taken) return false;
  state_->taken = true;
  *check = state_->check, *counts = state_->counts;
  if (unfinished) *unfinished = state_->unfinished;
  if (finished) *finished = state_->finished;
  return true;
}

}  // namespace rekit::sdl
