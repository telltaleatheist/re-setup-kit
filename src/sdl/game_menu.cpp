#include "rekit/sdl/game_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <set>

#include "rekit/backup.h"
#include "rekit/manifest.h"
#include "rekit/program_record.h"
#include "rekit/sdl/setup_original.h"
#include "setup_text.h"


namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

// The menu is laid out on a 1280x720 frame (the mockup's), scaled to fit the window and centred; the scrim covers it
// all. The hint is in the window's own logical pixels.
constexpr float kW = 1280, kH = 720;
constexpr int kHintStarts = 3;          // the first few starts show the hint
constexpr uint64_t kHintMs = 5000;      // for the first 5 seconds of each

enum Page { kFiles, kUpdates, kReport, kCheck, kReset, kData };
constexpr int kPages = 6;
const char* const kPageNames[kPages] = {"GAME FILES",     "UPDATES",        "REPORT A PROBLEM",
                                        "CHECK MY FILES", "RESET SETTINGS", "OPEN MY DATA FOLDER"};
constexpr int kListed = 6;  // a check's damaged files listed by name, the rest counted

enum Id {
  kNone = 0,
  kReturn = 1,
  kChange = 10, kChangeGo, kChangeKeep,
  kOpenLog = 20, kCopyLog, kOpenReport,
  kOpenData = 30, kCopyData,
  // 40..49: UPDATES (step 3)
  kCheckGo = 50, kCheckCancel,
  kResetAsk = 55, kResetGo, kResetKeep,
  kNav = 100,    // + page
  kEntry = 200,  // + entry
};

struct Button {
  int id = 0;
  SDL_FRect r{};
  std::string label;
  int col = 0;          // 0 the sections and entries (and RETURN), 1 the page's buttons
  bool accent = false;  // the page's main action
};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
bool env_set(const char* name) {  // SDL's copy of the environment, as the rest of the kit reads it
  const char* v = SDL_getenv(name);
  return v && *v;
}
bool kit_test() { return kit_test_mode(); }
const char* platform_words() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}
// A folder as a file:// address SDL_OpenURL takes on every system (the Mac's reads it as a URL: spaces escaped).
std::string folder_url(const std::string& path) {
  std::string p = fs::path(path).generic_string();
  std::string out = "file://";
  if (!p.empty() && p[0] != '/') out += '/';  // C:/Users/...
  static const char* hex = "0123456789ABCDEF";
  for (const char c : p) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') out += c;
    else out += '%', out += hex[u >> 4], out += hex[u & 15];
  }
  return out;
}
// Input the open menu keeps from the game (window, device and quit events still reach it).
bool is_input(Uint32 type) {
  switch (type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_EDITING: case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: case SDL_EVENT_JOYSTICK_BALL_MOTION: case SDL_EVENT_JOYSTICK_HAT_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN: case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN: case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION: case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
    case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
    case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_PEN_DOWN: case SDL_EVENT_PEN_UP: case SDL_EVENT_PEN_MOTION: case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP: case SDL_EVENT_PEN_AXIS: case SDL_EVENT_PEN_PROXIMITY_IN: case SDL_EVENT_PEN_PROXIMITY_OUT:
      return true;
    default:
      return false;
  }
}

struct Sound {
  SDL_AudioSpec spec{};
  Uint8* buf = nullptr;
  Uint32 len = 0;
  ~Sound() { SDL_free(buf); }
};

}  // namespace

struct GameMenu::Impl {
  MenuHooks h;
  Theme t;
  bool keys_on = !kit_test();
  // The menu.
  bool open = false;
  int page = kFiles;
  bool confirm_change = false;
  std::string report_url;
  std::string status;
  bool status_bad = false;
  int pending_entry = -1;  // picked: called after the menu closes
  // CHECK MY FILES: the program's files, then the game files, on a worker thread.
  struct Checking {
    std::thread th;
    std::mutex m;
    std::atomic<bool> cancel{false}, done{false};
    std::string which, path;  // what is being read now
    int n = 0, total = 0;
    std::vector<std::pair<std::string, FilesCheck>> results;  // each set's label and its check
    ~Checking() {
      cancel = true;
      if (th.joinable()) th.join();
    }
  };
  std::unique_ptr<Checking> checking;
  int check_drawn = -1;      // the progress last drawn
  uint64_t check_drawn_at = 0;
  bool confirm_reset = false;
  // What went down for one side goes up for that side. The game's held keys and buttons get their ups when the menu
  // opens (their real ups, and a key's repeats, are then eaten); what went down in the menu is eaten after it closes.
  using PadButton = std::pair<SDL_JoystickID, Uint8>;
  std::map<SDL_Scancode, SDL_Event> game_keys;
  std::map<Uint8, SDL_Event> game_mouse;
  std::map<PadButton, SDL_Event> game_pad;
  std::set<SDL_Scancode> menu_keys, eat_keys;
  std::set<Uint8> menu_mouse, eat_mouse;
  std::set<PadButton> menu_pad, eat_pad;
  // The hint.
  bool hint_on = false;
  uint64_t hint_until = 0;
  // The picture.
  SDL_Surface* surf = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* badge = nullptr;
  detail::Text bold, body;
  int pw = 0, ph = 0;            // the picture's pixels
  float lw = 0, lh = 0;          // the window's logical size
  float k = 1, ox = 0, oy = 0;   // design units to the window's logical pixels, the design frame's place
  bool dirty = true, drawing = false;
  enum Shown { kNothing, kMenu, kHint } shown = kNothing;
  uint64_t serial = 0;
  std::vector<Button> buttons;
  int hot = kNone;
  Sound click, hover;
  SDL_AudioStream* audio = nullptr;
  bool audio_tried = false;

  Impl(MenuHooks hooks, const Theme& theme) : h(std::move(hooks)), t(theme), report_url(h.report_url) {}
  ~Impl() {
    if (audio) SDL_DestroyAudioStream(audio);
    drop_picture();
  }

  void log(const std::string& s) {
    if (h.log) h.log(s);
    else std::printf("rekit: %s\n", s.c_str());
  }
  uint64_t now() const { return h.now_ms ? h.now_ms() : SDL_GetTicks(); }
  void deliver(const SDL_Event& e) {
    if (h.deliver) h.deliver(e);
  }

  // --- The hint: a count of starts in the user folder.
  void count_start() {
    if (kit_test() || env_set("RE_LAUNCHER") || h.user_folder.empty()) return;
    const fs::path f = fs::path(h.user_folder) / "setup-hint.txt";
    int n = 0;
    if (std::ifstream in(f); in) in >> n;
    if (n >= kHintStarts) return;
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    std::ofstream(f) << (n + 1) << "\n";
    hint_on = true;
  }

  // --- Open and close.
  void show(int p) {
    if (open) return;
    release_game();
    open = true, page = p, confirm_change = false, confirm_reset = false, status.clear(), pending_entry = -1, hint_on = false;
    hot = kNav + page, dirty = true;
    if (h.pause) h.pause();
    log("menu: open (" + std::string(kPageNames[page]) + ")");
  }
  void hide() {
    if (!open) return;
    open = false, dirty = true;
    if (checking && !checking->done) checking.reset();  // a check stops with the menu (the files are not read in play)
    eat_keys.insert(menu_keys.begin(), menu_keys.end()), eat_mouse.insert(menu_mouse.begin(), menu_mouse.end());
    eat_pad.insert(menu_pad.begin(), menu_pad.end());
    menu_keys.clear(), menu_mouse.clear(), menu_pad.clear();
    if (h.resume) h.resume();
    log("menu: closed");
    if (pending_entry >= 0 && pending_entry < static_cast<int>(h.entries.size())) {
      const MenuEntry& en = h.entries[pending_entry];
      pending_entry = -1;
      log("menu: the game's " + upper(en.label));
      if (en.pick) en.pick();
    }
  }

  // --- The keys.
  bool combo(const SDL_Event& e) const {
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.key != SDLK_F1 || e.key.repeat) return false;
    const SDL_Keymod m = e.key.mod;
#if defined(__APPLE__)
    return (m & SDL_KMOD_GUI) && (m & SDL_KMOD_SHIFT);
#else
    return (m & SDL_KMOD_ALT) && (m & SDL_KMOD_SHIFT);
#endif
  }
  // An up (or a key's repeat) the game must not see: its down went to the other side.
  bool eaten(const SDL_Event& e) {
    switch (e.type) {
      case SDL_EVENT_KEY_UP: return eat_keys.erase(e.key.scancode) > 0;
      case SDL_EVENT_KEY_DOWN:
        if (e.key.repeat) return eat_keys.count(e.key.scancode) > 0;
        eat_keys.erase(e.key.scancode);
        return false;
      case SDL_EVENT_MOUSE_BUTTON_UP: return eat_mouse.erase(e.button.button) > 0;
      case SDL_EVENT_GAMEPAD_BUTTON_UP: return eat_pad.erase({e.gbutton.which, e.gbutton.button}) > 0;
      default: return false;
    }
  }
  template <class Keys, class Mouse, class Pad>
  static void track(const SDL_Event& e, Keys&& keys, Mouse&& mouse, Pad&& pad) {
    switch (e.type) {
      case SDL_EVENT_KEY_DOWN: if (!e.key.repeat) keys(e.key.scancode, true); break;
      case SDL_EVENT_KEY_UP: keys(e.key.scancode, false); break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN: mouse(e.button.button, true); break;
      case SDL_EVENT_MOUSE_BUTTON_UP: mouse(e.button.button, false); break;
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN: pad(PadButton{e.gbutton.which, e.gbutton.button}, true); break;
      case SDL_EVENT_GAMEPAD_BUTTON_UP: pad(PadButton{e.gbutton.which, e.gbutton.button}, false); break;
      default: break;
    }
  }
  void track_game(const SDL_Event& e) {
    track(e, [&](SDL_Scancode s, bool d) { d ? void(game_keys[s] = e) : void(game_keys.erase(s)); },
          [&](Uint8 b, bool d) { d ? void(game_mouse[b] = e) : void(game_mouse.erase(b)); },
          [&](PadButton b, bool d) { d ? void(game_pad[b] = e) : void(game_pad.erase(b)); });
  }
  void track_menu(const SDL_Event& e) {
    track(e, [&](SDL_Scancode s, bool d) { d ? void(menu_keys.insert(s)) : void(menu_keys.erase(s)); },
          [&](Uint8 b, bool d) { d ? void(menu_mouse.insert(b)) : void(menu_mouse.erase(b)); },
          [&](PadButton b, bool d) { d ? void(menu_pad.insert(b)) : void(menu_pad.erase(b)); });
  }
  // The game's held keys and buttons (the combination's Shift and Alt among them) go up for it as the menu opens.
  void release_game() {
    for (auto [s, down] : game_keys) {
      down.type = SDL_EVENT_KEY_UP, down.key.type = SDL_EVENT_KEY_UP, down.key.down = false, down.key.repeat = false;
      deliver(down), eat_keys.insert(s);
    }
    for (auto [b, down] : game_mouse) {
      down.type = SDL_EVENT_MOUSE_BUTTON_UP, down.button.type = SDL_EVENT_MOUSE_BUTTON_UP, down.button.down = false;
      deliver(down), eat_mouse.insert(b);
    }
    for (auto [b, down] : game_pad) {
      down.type = SDL_EVENT_GAMEPAD_BUTTON_UP, down.gbutton.type = SDL_EVENT_GAMEPAD_BUTTON_UP, down.gbutton.down = false;
      deliver(down), eat_pad.insert(b);
    }
    game_keys.clear(), game_mouse.clear(), game_pad.clear();
  }
  bool on_event(const SDL_Event& e) {
    if (eaten(e)) return true;
    if (keys_on && combo(e)) {
      eat_keys.insert(e.key.scancode);  // F1's up
      if (open) hide();
      else show(kFiles);
      return true;
    }
    if (open) {
      if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
          e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        dirty = true;
      if (!is_input(e.type)) {
        deliver(e);
        return false;
      }
      track_menu(e);
      menu_input(e);
      return true;
    }
    track_game(e);
    if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) dirty = true;
    deliver(e);
    return false;
  }

  // --- The menu's own input: keys, a pad, the mouse.
  const Button* button(int id) const {
    for (const Button& b : buttons)
      if (b.id == id) return &b;
    return nullptr;
  }
  void set_hot(int id) {
    if (id == hot) return;
    hot = id, dirty = true;
    if (id >= kNav && id < kNav + kPages && page != id - kNav)
      page = id - kNav, confirm_change = false, confirm_reset = false, status.clear();
    if (id != kNone) play(hover, 0.7f);
  }
  void step(int dir, bool across) {
    layout();
    const Button* cur = button(hot);
    const int col = cur ? cur->col : 0;
    std::vector<int> ring;
    for (const Button& b : buttons)
      if (across || b.col == col) ring.push_back(b.id);
    if (ring.empty()) return;
    auto at = std::find(ring.begin(), ring.end(), hot);
    const int n = static_cast<int>(ring.size());
    const int i = at == ring.end() ? (dir > 0 ? -1 : 0) : static_cast<int>(at - ring.begin());
    set_hot(ring[((i + dir) % n + n) % n]);
  }
  void into_page() {
    layout();
    for (const Button& b : buttons)
      if (b.col == 1) return set_hot(b.id);
  }
  void back() {
    const Button* cur = button(hot);
    if (cur && cur->col == 1) set_hot(kNav + page);
    else hide();
  }
  void menu_input(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN) {
      switch (e.key.key) {
        case SDLK_ESCAPE:
          if (!e.key.repeat) hide();
          return;
        case SDLK_UP: return step(-1, false);
        case SDLK_DOWN: return step(1, false);
        case SDLK_TAB: return step((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1, true);
        case SDLK_RIGHT: return into_page();
        case SDLK_LEFT: return back_to_nav();
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
          if (!e.key.repeat) press(hot);
          return;
        default: return;
      }
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
      switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return step(-1, false);
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return step(1, false);
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return into_page();
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return back_to_nav();
        case SDL_GAMEPAD_BUTTON_SOUTH: return press(hot);
        case SDL_GAMEPAD_BUTTON_EAST: return back();
        case SDL_GAMEPAD_BUTTON_START: return hide();
        default: return;
      }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      layout();
      const bool motion = e.type == SDL_EVENT_MOUSE_MOTION;
      const float x = motion ? e.motion.x : e.button.x, y = motion ? e.motion.y : e.button.y;
      const SDL_FPoint p{(x - ox) / k, (y - oy) / k};
      int under = kNone;
      for (const Button& b : buttons)
        if (SDL_PointInRectFloat(&p, &b.r)) under = b.id;
      if (e.type == SDL_EVENT_MOUSE_MOTION) {
        if (under != kNone && under != hot) {  // the pointer lights what it is over; a section shows its page on a click
          hot = under, dirty = true;
          play(hover, 0.7f);
        }
      } else if (e.button.button == SDL_BUTTON_LEFT && under != kNone) {
        hot = under, dirty = true;
        press(under);
      }
    }
  }
  void back_to_nav() {
    const Button* cur = button(hot);
    if (cur && cur->col == 1) set_hot(kNav + page);
  }

  bool open_url(const std::string& url) { return h.open_url ? h.open_url(url) : SDL_OpenURL(url.c_str()); }
  bool copy_text(const std::string& s) { return h.copy_text ? h.copy_text(s) : SDL_SetClipboardText(s.c_str()); }
  void say(const std::string& s, bool bad) { status = s, status_bad = bad, dirty = true; }
  void open_folder(const std::string& dir, const char* what) {
    log(std::string("menu: open ") + what + ": " + dir);
    if (!open_url(folder_url(dir))) say("Could not open " + dir + ".", true);
    else say("", false);
  }
  void copy(const std::string& s) {
    log("menu: copy: " + s);
    if (copy_text(s)) say("Copied: " + s, false);
    else say("Could not copy the path.", true);
  }
  std::string log_folder() const { return h.log_file.empty() ? "" : fs::path(h.log_file).parent_path().string(); }

  void press(int id) {
    if (id == kNone) return;
    play(click, 1.0f);
    if (id >= kNav && id < kNav + kPages) {
      page = id - kNav, confirm_change = false, confirm_reset = false, status.clear(), dirty = true;
      into_page();
      if (!button(hot) || button(hot)->col != 1) hot = id;
      return;
    }
    if (id >= kEntry && id < kEntry + static_cast<int>(h.entries.size())) {
      pending_entry = id - kEntry;
      hide();
      return;
    }
    switch (id) {
      case kReturn: return hide();
      case kChange: confirm_change = true, dirty = true, hot = kChangeKeep; return;
      case kChangeKeep: confirm_change = false, dirty = true, hot = kChange; return;
      case kChangeGo:
        log("menu: CHANGE: the game closes for its setup");
        confirm_change = false;
        hide();
        if (h.restart_for_setup) h.restart_for_setup();
        return;
      case kOpenLog: return open_folder(log_folder(), "the log folder");
      case kCopyLog: return copy(h.log_file);
      case kOpenReport:
        log("menu: open the report page: " + report_url);
        if (!open_url(report_url)) say("Could not open " + report_url + ".", true);
        return;
      case kCheckGo: return start_check();
      case kCheckCancel:
        log("menu: check cancelled");
        checking.reset(), dirty = true, hot = kCheckGo;
        return;
      case kResetAsk: confirm_reset = true, dirty = true, hot = kResetKeep; return;
      case kResetKeep: confirm_reset = false, dirty = true, hot = kResetAsk; return;
      case kResetGo: return reset_settings();
      case kOpenData: return open_folder(h.user_folder, "the data folder");
      case kCopyData: return copy(h.user_folder);
      default: return;
    }
  }

  // --- CHECK MY FILES.
  void start_check() {
    checking = std::make_unique<Checking>();
    Checking* c = checking.get();
    std::vector<std::pair<std::string, std::string>> sets;  // label, root
    if (!h.program_dir.empty()) sets.push_back({"The program", h.program_dir});
    if (!h.files_in_use.empty()) sets.push_back({"The game files", h.files_in_use});
    const std::vector<LeaveOut> leave = h.leave_out;
    log("menu: checking the files");
    c->th = std::thread([c, sets, leave] {
      for (const auto& [label, root] : sets) {
        {
          std::lock_guard<std::mutex> g(c->m);
          c->which = label, c->n = 0, c->total = 0, c->path.clear();
        }
        FilesCheck r = check_files(root, leave, [c](int n, int total, const std::string& path) {
          std::lock_guard<std::mutex> g(c->m);
          c->n = n, c->total = total, c->path = path;
          return !c->cancel.load();
        });
        std::lock_guard<std::mutex> g(c->m);
        c->results.push_back({label, std::move(r)});
        if (c->cancel) break;
      }
      c->done = true;
    });
    hot = kCheckCancel, dirty = true;
  }
  // The check's words for one set.
  std::vector<std::pair<std::string, bool>> check_words(const std::string& label, const FilesCheck& r) const {
    std::vector<std::pair<std::string, bool>> out;  // a line, whether it is about a problem
    if (!r.has_manifest) {
      out.push_back({label + ": no record of these files to check them against (a development build, or files "
                     "installed before records were kept).", false});
      return out;
    }
    if (!r.error.empty()) return out.push_back({label + ": " + r.error + ".", true}), out;
    const size_t bad = r.missing.size() + r.changed.size();
    if (bad == 0) {
      out.push_back({label + ": all " + std::to_string(r.checked) + " files are as they were installed.", false});
      return out;
    }
    std::string head = label + ": ";
    if (!r.changed.empty()) head += std::to_string(r.changed.size()) + (r.changed.size() == 1 ? " file changed" : " files changed");
    if (!r.changed.empty() && !r.missing.empty()) head += ", ";
    if (!r.missing.empty()) head += std::to_string(r.missing.size()) + " missing";
    out.push_back({head + ".", true});
    size_t listed = 0;
    for (const std::string& f : r.changed)
      if (listed++ < kListed) out.push_back({"    " + f + " (changed)", true});
    for (const std::string& f : r.missing)
      if (listed++ < kListed) out.push_back({"    " + f + " (missing)", true});
    if (bad > kListed) out.push_back({"    and " + std::to_string(bad - kListed) + " more (the log lists them all)", true});
    return out;
  }
  void finish_check() {
    if (!checking || !checking->done || checking->th.joinable() == false) return;
    checking->th.join();
    for (const auto& [label, r] : checking->results) {
      std::string line = "menu: check: " + label + ": ";
      if (!r.has_manifest) line += "no record";
      else if (!r.error.empty()) line += r.error;
      else line += std::to_string(r.checked) + " as installed, " + std::to_string(r.changed.size()) + " changed, " +
                   std::to_string(r.missing.size()) + " missing";
      log(line);
      for (const std::string& f : r.changed) log("menu: check: changed: " + f);
      for (const std::string& f : r.missing) log("menu: check: missing: " + f);
    }
    if (hot == kCheckCancel) hot = kCheckGo;
    dirty = true;
  }

  // --- RESET SETTINGS: a copy first, then the game's own reset (it is running: only it can).
  void reset_settings() {
    confirm_reset = false, dirty = true, hot = kResetAsk;
    const Backup b = back_up(h.user_folder, h.settings, "settings", 5);
    if (!b.ok) {
      log("menu: reset: no copy, nothing reset: " + b.error);
      return say("Nothing was reset: the settings could not be copied first. " + b.error, true);
    }
    log("menu: reset: settings copied to " + b.folder);
    std::error_code ec;  // said from the data folder down ("Backups/2026-11-27 20.37.41 settings"), as the player finds it
    std::string where = fs::relative(fs::path(b.folder), fs::path(h.user_folder), ec).generic_string();
    if (ec || where.empty() || where.rfind("..", 0) == 0) where = b.folder;
    if (!h.reset_settings || !h.reset_settings()) {
      log("menu: reset: the game could not reset its settings");
      return say("The settings could not be reset. A copy is in " + where + " in your data folder.", true);
    }
    log("menu: reset: done");
    say("The settings are back to their defaults. The old ones are in " + where + " in your data folder.", false);
  }

  // --- Sound, through its own stream when the game has SDL's audio up.
  void load_sounds() {
    auto load = [](Sound& s, const std::vector<unsigned char>& bytes) {
      if (bytes.empty()) return;
      SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
      if (io && !SDL_LoadWAV_IO(io, true, &s.spec, &s.buf, &s.len)) s.buf = nullptr, s.len = 0;
    };
    load(click, theme_sound(t, false));
    load(hover, theme_sound(t, true));
  }
  void play(const Sound& s, float gain) {
    if (!s.buf || !SDL_WasInit(SDL_INIT_AUDIO)) return;
    if (!audio && !audio_tried) {
      audio_tried = true;
      audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s.spec, nullptr, nullptr);
      if (audio) SDL_ResumeAudioStreamDevice(audio);
    }
    if (!audio) return;
    SDL_SetAudioStreamFormat(audio, &s.spec, nullptr);
    SDL_SetAudioStreamGain(audio, gain);
    SDL_ClearAudioStream(audio);
    SDL_PutAudioStreamData(audio, s.buf, static_cast<int>(s.len));
  }

  // --- The picture.
  void drop_picture() {
    if (badge) SDL_DestroyTexture(badge), badge = nullptr;
    if (ren) SDL_DestroyRenderer(ren), ren = nullptr;
    if (surf) SDL_DestroySurface(surf), surf = nullptr;
  }
  bool picture() {
    int w = 0, hgt = 0, iw = 0, ih = 0;
    if (!h.window || !SDL_GetWindowSizeInPixels(h.window, &w, &hgt) || !SDL_GetWindowSize(h.window, &iw, &ih) || w <= 0 ||
        hgt <= 0 || iw <= 0 || ih <= 0)
      return false;
    lw = static_cast<float>(iw), lh = static_cast<float>(ih);
    if (surf && w == pw && hgt == ph) return true;
    drop_picture();
    pw = w, ph = hgt, dirty = true;
    if (!(surf = SDL_CreateSurface(pw, ph, SDL_PIXELFORMAT_RGBA32)) || !(ren = SDL_CreateSoftwareRenderer(surf))) {
      log(std::string("menu: no picture: ") + SDL_GetError());
      drop_picture();
      return false;
    }
    if (!t.badge.empty())
      if (SDL_Surface* s = detail::load_png(t.badge)) {
        badge = SDL_CreateTextureFromSurface(ren, s);
        SDL_DestroySurface(s);
      }
    return true;
  }
  float density() const { return lw > 0 ? static_cast<float>(pw) / lw : 1.0f; }

  // Design units (the 1280x720 frame) to the picture: every drawing helper below takes them.
  void fill(const SDL_FRect& r, SDL_Color c) {
    if (!drawing) return;
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_FRect o{r.x + ox / k, r.y + oy / k, r.w, r.h};
    SDL_RenderFillRect(ren, &o);
  }
  void edge(const SDL_FRect& r, SDL_Color c) {
    fill({r.x, r.y, r.w, 1}, c), fill({r.x, r.y + r.h - 1, r.w, 1}, c);
    fill({r.x, r.y, 1, r.h}, c), fill({r.x + r.w - 1, r.y, 1, r.h}, c);
  }
  void words(detail::Text& f, const std::string& s, int size, SDL_Color c, float x, float y, int align = -1) {
    if (drawing) f.draw(ren, s, size, c, static_cast<int>(x + ox / k), static_cast<int>(y + oy / k), align);
  }
  // Words wrapped to `w` from (x, top); the height used.
  float para(detail::Text& f, const std::string& s, int size, SDL_Color c, float x, float top, float w) {
    const auto lines = f.wrap(s, size, static_cast<int>(w));
    const float skip = static_cast<float>(f.line_skip(size));
    for (size_t i = 0; i < lines.size(); ++i) words(f, lines[i], size, c, x, top + skip * (static_cast<float>(i) + 0.5f));
    return skip * static_cast<float>(lines.size());
  }
  void add(int id, SDL_FRect r, const std::string& label, int col, bool accent = false) {
    buttons.push_back({id, r, label, col, accent});
  }
  // A page's button at (x, y), as wide as its words; its right edge.
  float page_button(int id, const std::string& label, float x, float y, bool accent = false) {
    const int size = accent ? 19 : 17;
    const float w = static_cast<float>(bold.width(label, size)) + (accent ? 48 : 36), hh = accent ? 48.0f : 44.0f;
    add(id, {x, y, w, hh}, label, 1, accent);
    return x + w;
  }

  // The layout: every button's place, for the mouse and the keys (drawn by draw_menu from the same pass).
  void layout() {
    if (!surf && !picture()) return;
    k = std::min(lw / kW, lh / kH), ox = (lw - kW * k) / 2, oy = (lh - kH * k) / 2;
    const bool was = drawing;
    drawing = false;
    menu_pass();
    drawing = was;
  }
  void menu_pass() {
    buttons.clear();
    const float px = 180, py = 70, pwid = 920, phei = 580;
    // The panel, its header band and the sections' column.
    fill({px, py, pwid, phei}, t.panel);
    fill({px, py, pwid, 72}, t.band_colour);
    edge({px, py, pwid, phei}, t.rule);
    fill({px + 1, py + 72, pwid - 2, 1}, t.rule);
    fill({px + 230, py + 73, 1, phei - 74}, t.rule);
    // The header: the badge, the game's name, SETUP · THE GAME IS PAUSED, RETURN TO GAME.
    float tx = px + 24;
    if (badge && drawing) {
      SDL_FRect d{tx + ox / k, py + 16 + oy / k, 40, 40};
      SDL_RenderTexture(ren, badge, nullptr, &d);
    }
    if (badge) tx += 54;
    words(bold, t.header.empty() ? upper(h.title) : t.header, 28, t.text, tx, py + 30);
    words(bold, "SETUP  \xC2\xB7  THE GAME IS PAUSED", 15, t.accent, tx, py + 54);
    const std::string ret = "RETURN TO GAME  \xC2\xB7  Esc";
    const float rw = static_cast<float>(bold.width(ret, 17)) + 36;
    // The sections, then the game's own entries, then RETURN TO GAME (the header's button) in the keys' order.
    float y = py + 86;
    for (int p = 0; p < kPages; ++p, y += 46) add(kNav + p, {px + 1, y, 229, 44}, kPageNames[p], 0);
    if (!h.entries.empty()) {
      fill({px + 22, y + 4, 186, 1}, t.rule);
      y += 12;
      for (size_t i = 0; i < h.entries.size(); ++i, y += 46)
        add(kEntry + static_cast<int>(i), {px + 1, y, 229, 44}, upper(h.entries[i].label), 0);
    }
    add(kReturn, {px + pwid - 24 - rw, py + 14, rw, 44}, ret, 0);
    // The page.
    const float x = px + 258, w = pwid - 258 - 28;
    float top = py + 92;
    words(bold, kPageNames[page], 24, t.text, x, top + 14);
    top += 44;
    switch (page) {
      case kFiles: files_page(x, top, w); break;
      case kUpdates: break;  // UPDATES: step 3
      case kReport: report_page(x, top, w); break;
      case kCheck: check_page(x, top, w); break;
      case kReset: reset_page(x, top, w); break;
      case kData: data_page(x, top, w); break;
    }
    if (!status.empty()) {  // at the panel's foot, as many lines as it takes
      const float hgt = static_cast<float>(body.wrap(status, 16, static_cast<int>(w)).size() * body.line_skip(16));
      para(body, status, 16, status_bad ? t.bad : t.good, x, py + phei - 18 - hgt, w);
    }
    for (const Button& b : buttons) draw_button(b);
  }
  // An inset box from `top` holding what `inside` draws (given its left, top and width; it returns the height).
  float well(float x, float top, float w, const std::function<float(float, float, float)>& inside) {
    const bool was = drawing;
    const size_t keep = buttons.size();
    drawing = false;
    const float hh = inside(x + 16, top + 14, w - 32) + 28;  // measured first (its buttons dropped), then drawn on its box
    buttons.resize(keep);
    drawing = was;
    fill({x, top, w, hh}, t.well);
    edge({x, top, w, hh}, t.rule);
    inside(x + 16, top + 14, w - 32);
    return hh;
  }
  float path_line(const std::string& path, float x, float top, float w) {
    words(body, body.fit(path, 17, static_cast<int>(w)), 17, t.text, x, top + 11);
    return 24;
  }
  void files_page(float x, float top, float w) {
    top += para(body, "The game files in use:", 17, t.dim, x, top, w) + 8;
    top += well(x, top, w, [&](float ix, float it, float iw) {
             return path_line(h.files_in_use.empty() ? "Not known." : h.files_in_use, ix, it, iw);
           }) + 18;
    if (!h.restart_for_setup) return;
    const std::string name = h.title.empty() ? "the game" : h.title;
    if (!confirm_change) {
      top += para(body, "Other game files need a restart: " + name + " closes, and its setup opens in its own window.", 17,
                  t.dim, x, top, w) + 12;
      page_button(kChange, "CHANGE (RESTARTS THE GAME)", x, top);
      return;
    }
    top += para(body, "Close " + name + " and choose other game files? Anything not saved is lost.", 17, t.bad, x, top, w) + 12;
    const float r = page_button(kChangeGo, "CLOSE AND CHANGE", x, top - 2, true);
    page_button(kChangeKeep, "KEEP PLAYING", r + 12, top);
  }
  void report_page(float x, float top, float w) {
    top += para(body, "Something went wrong? Two steps: attach your log, and say what happened and how to make it happen again.",
                17, t.dim, x, top, w) + 14;
    top += well(x, top, w, [&](float ix, float it, float iw) {
             float y = it;
             words(bold, "1  \xC2\xB7  YOUR LOG", 15, t.accent, ix, y + 9);
             y += 26;
             if (h.log_file.empty()) return y - it + para(body, "This game keeps no log.", 16, t.dim, ix, y, iw);
             y += para(body, fs::path(h.log_file).filename().string() + ", in", 16, t.dim, ix, y, iw) + 2;
             y += path_line(log_folder(), ix, y, iw) + 10;
             const float r = page_button(kOpenLog, "OPEN THE LOG FOLDER", ix, y);
             page_button(kCopyLog, "COPY THE PATH", r + 10, y);
             return y + 44 - it;
           }) + 14;
    well(x, top, w, [&](float ix, float it, float iw) {
      float y = it;
      words(bold, "2  \xC2\xB7  DESCRIBE IT", 15, t.accent, ix, y + 9);
      y += 26;
      const std::string you = (h.title.empty() ? std::string("this game") : h.title) +
                              (h.version.empty() ? "" : " " + h.version) + " on " + platform_words();
      if (report_url.empty()) return y - it + para(body, "No page for reports is known. You have " + you + ".", 16, t.dim, ix, y, iw);
      y += para(body, "Opens the page where problems are reported, in your browser. Say you have " + you + ".", 16, t.dim, ix,
                y, iw) + 10;
      const float r = page_button(kOpenReport, "OPEN THE REPORT PAGE", ix, y, true);
      words(body, body.fit(report_url, 15, static_cast<int>(ix + iw - r - 14)), 15, t.dim, r + 14, y + 24);
      return y + 48 - it;
    });
  }
  void check_page(float x, float top, float w) {
    top += para(body, "Checks the program and the game files against the record made when they were installed. Mods, "
                "saves and anything added later are not looked at.", 17, t.dim, x, top, w) + 14;
    if (checking && !checking->done) {
      std::string which, path;
      int n = 0, total = 0;
      {
        std::lock_guard<std::mutex> g(checking->m);
        which = checking->which, path = checking->path, n = checking->n, total = checking->total;
      }
      check_drawn = n;
      top += para(body, total ? which + ": " + std::to_string(n) + " of " + std::to_string(total) + " files" : which + "...",
                  17, t.text, x, top, w) + 6;
      fill({x, top, w, 8}, t.well), edge({x, top, w, 8}, t.rule);
      if (total) fill({x + 1, top + 1, (w - 2) * static_cast<float>(n) / static_cast<float>(total), 6}, t.accent);
      top += 16;
      words(body, body.fit(path, 15, static_cast<int>(w)), 15, t.dim, x, top + 9);
      top += 30;
      page_button(kCheckCancel, "CANCEL", x, top);
      return;
    }
    if (h.program_dir.empty() && h.files_in_use.empty()) {
      para(body, "This game gives no files to check.", 17, t.dim, x, top, w);
      return;
    }
    page_button(kCheckGo, checking ? "CHECK AGAIN" : "CHECK NOW", x, top, true);
    top += 62;
    if (!checking) return;
    bool game_damage = false, program_damage = false;
    for (const auto& [label, r] : checking->results) {
      for (const auto& [line, bad] : check_words(label, r)) {
        const bool item = line.rfind("    ", 0) == 0;  // a file named under its set, indented
        top += para(body, item ? line.substr(4) : line, 16, bad ? t.bad : t.good, item ? x + 20 : x, top, item ? w - 20 : w) + 2;
      }
      top += 8;
      if (r.has_manifest && !r.ok() && !r.cancelled) (label == "The game files" ? game_damage : program_damage) = true;
    }
    const std::string name = h.title.empty() ? "the game" : h.title;
    if (game_damage) top += para(body, "To put the game files right, GAME FILES' CHANGE installs them again.", 16, t.dim, x, top, w) + 4;
    if (program_damage)
      para(body, "To put the program right, install " + name + " again over this copy, from its release page.", 16, t.dim, x, top, w);
  }
  void reset_page(float x, float top, float w) {
    const std::string name = h.title.empty() ? "This game" : h.title;
    if (!h.reset_settings) {
      para(body, name + " resets its settings in its own options.", 17, t.dim, x, top, w);
      return;
    }
    top += para(body, "Puts every setting back to its default: controls, video, sound and the game's options. Your "
                "saves and pilots stay as they are. A copy of the settings is kept first, in Backups in your data folder.",
                17, t.dim, x, top, w) + 14;
    if (!confirm_reset) {
      page_button(kResetAsk, "RESET SETTINGS", x, top);
      return;
    }
    top += para(body, "Reset every setting to its default?", 17, t.bad, x, top, w) + 12;
    const float r = page_button(kResetGo, "RESET", x, top - 2, true);
    page_button(kResetKeep, "KEEP THEM", r + 12, top);
  }
  void data_page(float x, float top, float w) {
    top += para(body, "Your saves, settings, logs, music and mods are in:", 17, t.dim, x, top, w) + 8;
    top += well(x, top, w, [&](float ix, float it, float iw) {
             return path_line(h.user_folder.empty() ? "Not known." : h.user_folder, ix, it, iw);
           }) + 18;
    if (h.user_folder.empty()) return;
    const float r = page_button(kOpenData, "OPEN THE FOLDER", x, top, true);
    page_button(kCopyData, "COPY THE PATH", r + 12, top + 2);
  }
  void draw_button(const Button& b) {
    const bool lit = b.id == hot;
    if (b.id >= kNav && b.id < kEntry + 100 && b.id != kReturn) {  // a section or an entry: a row of the column
      const bool current = b.id == kNav + page;
      if (current) fill(b.r, t.nav_on), fill({b.r.x, b.r.y, 3, b.r.h}, t.accent);
      else if (lit) fill(b.r, t.nav_on);
      words(bold, b.label, 18, current ? t.text : lit ? t.accent : t.dim, b.r.x + 21, b.r.y + b.r.h / 2);
      if (lit && current) edge(b.r, t.accent);
      return;
    }
    const int size = b.accent ? 19 : 17;
    if (b.accent) {
      fill(b.r, t.accent);
      if (lit) edge(b.r, t.text);
      words(bold, b.label, size, t.on_accent, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2, 0);
    } else {
      fill(b.r, t.plate_colour);
      edge(b.r, lit ? t.accent : t.plate_edge);
      words(bold, b.label, size, lit ? t.accent : t.plate_text, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2, 0);
    }
  }

  void draw_menu() {
    layout();
    const float s = density() * k;
    bold.set_scale(s), body.set_scale(s);
    SDL_SetRenderScale(ren, s, s);
    SDL_SetRenderDrawColor(ren, t.scrim.r, t.scrim.g, t.scrim.b, t.scrim.a);  // the dim over the whole game
    SDL_RenderClear(ren);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    drawing = true;
    menu_pass();
    drawing = false;
    SDL_RenderPresent(ren);
  }
  void draw_hint() {
    const float s = density();
    bold.set_scale(s), body.set_scale(s);
    SDL_SetRenderScale(ren, s, s);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
    const std::string a = "SETUP", b = keys_words();
    const float w = static_cast<float>(bold.width(a, 15) + body.width(b, 15)) + 8 + 20, hh = 28;
    const SDL_FRect box{20, lh - 16 - hh, w, hh};
    SDL_SetRenderDrawColor(ren, 8, 11, 16, 158);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const SDL_Color e{t.accent.r, t.accent.g, t.accent.b, 115};
    SDL_SetRenderDrawColor(ren, e.r, e.g, e.b, e.a);
    SDL_RenderRect(ren, &box);
    bold.draw(ren, a, 15, t.accent, static_cast<int>(box.x + 10), static_cast<int>(box.y + hh / 2), -1);
    body.draw(ren, b, 15, t.dim, static_cast<int>(box.x + 18 + bold.width(a, 15)), static_cast<int>(box.y + hh / 2), -1);
    SDL_RenderPresent(ren);
  }

  void frame() {
    const uint64_t n = now();
    if (hint_on && hint_until == 0) hint_until = n + kHintMs;
    if (hint_on && n >= hint_until) hint_on = false;
    if (checking && checking->done && checking->th.joinable()) finish_check();
    if (checking && !checking->done && open && page == kCheck && n - check_drawn_at >= 200) {
      std::lock_guard<std::mutex> g(checking->m);
      if (checking->n != check_drawn) dirty = true, check_drawn_at = n;  // the bar, five times a second at most
    }
    const Shown want = open ? kMenu : hint_on ? kHint : kNothing;
    if (want == kNothing) {
      shown = kNothing;
      return;
    }
    if (!picture()) return;
    if (want != shown) dirty = true;
    if (dirty) {
      want == kMenu ? draw_menu() : draw_hint();
      dirty = false, shown = want, ++serial;
    }
    if (h.draw) h.draw({surf->pixels, surf->w, surf->h, surf->pitch, serial});
  }
};

GameMenu::GameMenu(MenuHooks hooks, const Theme& theme) : impl_(std::make_unique<Impl>(std::move(hooks), theme)) {
  Impl& m = *impl_;
  const std::vector<unsigned char> b = theme_font(m.t, false), f = theme_font(m.t, true);
  if (!b.empty()) m.bold.open_memory(b.data(), b.size());
  if (!f.empty()) m.body.open_memory(f.data(), f.size());
  m.load_sounds();
  m.count_start();
  m.log(std::string("menu: ready (") + (m.keys_on ? keys_words() : std::string("keys off: RE_KIT_TEST")) +
        (m.hint_on ? ", the hint shows" : "") + ")");
}
GameMenu::~GameMenu() = default;

bool GameMenu::event(const SDL_Event& e) { return impl_->on_event(e); }
void GameMenu::frame() { impl_->frame(); }
bool GameMenu::is_open() const { return impl_->open; }
void GameMenu::show() { impl_->show(kFiles); }
void GameMenu::hide() { impl_->hide(); }
void GameMenu::set_report_url(const std::string& url) {
  if (url.rfind("https://", 0) != 0) return;
  impl_->report_url = url, impl_->dirty = true;
}

SDL_Keymod GameMenu::keys() {
#if defined(__APPLE__)
  return SDL_KMOD_GUI | SDL_KMOD_SHIFT;
#else
  return SDL_KMOD_ALT | SDL_KMOD_SHIFT;
#endif
}
std::string GameMenu::keys_words() {
#if defined(__APPLE__)
  return "Cmd + Shift + F1";
#else
  return "Shift + Alt + F1";
#endif
}

bool relaunch_for_setup(const std::vector<std::string>& args) {
  const std::string self = self_path();  // $APPIMAGE when this runs from it; "" when translocated (a Mac app run from Downloads)
  if (self.empty()) return false;
  std::vector<const char*> argv{self.c_str()};
  for (const std::string& a : args) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  SDL_Process* p = SDL_CreateProcess(argv.data(), false);
  if (!p) return false;
  SDL_DestroyProcess(p);  // not waited for: it outlives this program
  return true;
}

}  // namespace rekit::sdl
