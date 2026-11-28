#include "rekit/sdl/game_menu.h"
#include "rekit/settings.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "rekit/backup.h"
#include "rekit/manifest.h"
#include "rekit/original.h"
#include "rekit/program_record.h"
#include "rekit/sdl/setup_original.h"
#include "setup_text.h"


namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

// The menu is laid out on a 1280x720 frame (the mockup's), scaled to fit the window and centred; the scrim covers it
// all. The hint is in the window's own logical pixels.
constexpr float kW = 1280, kH = 720;
// The hint's starts, time and fades, and the other tunables: setup-kit-settings.json (rekit/settings.h).
int hint_starts() { return rekit::kit_settings().hint_starts; }
uint64_t ms(double seconds) { return static_cast<uint64_t>(std::llround(seconds * 1000)); }
uint64_t hint_ms() { return ms(rekit::kit_settings().hint_seconds); }
uint64_t hint_in_ms() { return std::max<uint64_t>(1, ms(rekit::kit_settings().hint_fade_in_seconds)); }
uint64_t hint_out_ms() { return std::max<uint64_t>(1, ms(rekit::kit_settings().hint_fade_out_seconds)); }
uint64_t notice_ms() { return ms(rekit::kit_settings().update_notice_seconds); }

enum Page { kFiles, kUpdates, kReport, kCheck, kReset, kData };
constexpr int kPages = 6;
const char* const kPageNames[kPages] = {"GAME FILES",     "UPDATES",        "REPORT A PROBLEM",
                                        "CHECK MY FILES", "RESET SETTINGS", "OPEN MY DATA FOLDER"};
size_t listed() { return static_cast<size_t>(rekit::kit_settings().check_listed); }  // named, the rest counted

enum Id {
  kNone = 0,
  kReturn = 1,
  kChange = 10, kChangeGo, kChangeKeep,
  kOpenLog = 20, kCopyLog, kOpenReport,
  kOpenData = 30, kCopyData,
  kUpdBox = 40, kUpdCheckNow, kUpdNow, kUpdGo, kUpdKeep, kUpdPage, kPromptGo, kPromptLater, kPromptNever, kPromptPage,
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

// A size as players read it ("34 MB").
std::string megabytes(uint64_t bytes) {
  return std::to_string(std::max<uint64_t>(1, (bytes + 500000) / 1000000)) + " MB";
}
// A release's notes (Markdown) as lines: "- x" and "* x" as bullets, "# x" as a heading (bold), emphasis marks dropped.
std::vector<std::pair<bool, std::string>> note_lines(const std::string& md) {
  std::vector<std::pair<bool, std::string>> out;
  size_t at = 0;
  while (at <= md.size()) {
    size_t end = md.find('\n', at);
    if (end == std::string::npos) end = md.size();
    std::string line = md.substr(at, end - at);
    at = end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    size_t lead = line.find_first_not_of(' ');
    if (lead == std::string::npos) continue;
    line.erase(0, lead);
    bool heading = false;
    if (line[0] == '#') heading = true, line.erase(0, line.find_first_not_of("# "));
    else if (line.size() > 1 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') line = "\xE2\x80\xA2 " + line.substr(2);
    for (const char* mark : {"**", "__", "`"})
      for (size_t m; (m = line.find(mark)) != std::string::npos;) line.erase(m, std::char_traits<char>::length(mark));
    if (!line.empty()) out.push_back({heading, line});
  }
  return out;
}
std::string today() {
  char buf[16] = {};
  const std::time_t t = std::time(nullptr);
  if (const std::tm* tm = std::localtime(&t)) std::strftime(buf, sizeof buf, "%Y-%m-%d", tm);
  return buf;
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
  std::vector<bool> entries_drawn;  // each entry's available() as last drawn
  // CHECK MY FILES: the program's files, then the game files, on a worker thread.
  struct Checking {
    std::thread th;
    std::mutex m;
    std::atomic<bool> cancel{false}, done{false};
    std::string which, path;  // what is being read now
    int n = 0, total = 0;
    struct Set {
      std::string label;
      FilesCheck r;
      bool by_game = false;               // no record: the game's own check (check_game_files) ran
      std::vector<std::string> problems;  // what it found, in its words
      bool recorded = false;              // it found nothing, and the record was written
    };
    std::vector<Set> results;
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
  bool runs_on_drawn = false;  // the header's NOT PAUSED (hooks.runs_on) as last drawn
  // The hint.
  bool hint_on = false, hint_started = false;
  uint64_t hint_from = 0, hint_closed = 0;  // its first frame; when its × was clicked (0: not)
  SDL_FRect hint_x{};                       // the ×'s hit area, the window's logical pixels (w 0: not drawn)
  uint8_t hint_alpha = 0;                   // the alpha drawn
  uint8_t hint_x_button = 0;                // the mouse button whose down the × took (its up is taken too)
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
  SDL_AudioStream* audio[2] = {};  // click, hover: each its own, so a hover never cuts a click off
  bool audio_tried = false;
  // UPDATES: the start-up check, the last check's verdict, the prompt, and CHECK NOW / UPDATE NOW on a worker thread
  // (its results taken in frame(), on the game's thread).
  std::unique_ptr<StartUpdateCheck> update_check;
  UpdatePrefs prefs;
  UpdateCheck upd;
  std::string upd_counts;
  bool upd_checked = false, confirm_update = false, prompt = false;
  // The start-up check's find, in the hint's corner (2026-11-28, in place of the prompt over the game): its keys open
  // the prompt for the rest of this start.
  bool notice = false, prompt_waits = false;
  std::string blocked;  // update_blocked's words, found once per check
  std::string unfinished;  // the version an earlier UPDATE NOW did not reach: said once, in the prompt
  enum Work { kIdle, kChecking, kUpdating } work = kIdle;
  std::thread worker;
  std::atomic<bool> work_stop{false}, work_done{false};
  std::atomic<int> work_permille{0};
  int shown_permille = -1;
  std::mutex work_m;
  UpdateCheck work_check;
  std::string work_counts, work_error;
  UpdateInstall work_install;

  Impl(MenuHooks hooks, const Theme& theme) : h(std::move(hooks)), t(theme), report_url(h.report_url) {
    std::string dir = h.program_dir;
    if (dir.empty())
      if (const char* base = SDL_GetBasePath()) dir = base;
    rekit::load_kit_settings(dir, h.user_folder, [this](const std::string& l) { log(l); });
    const rekit::KitSettings ks = rekit::kit_settings();
    if (!ks.catalogs.empty() && !h.update.allow_file) h.update.catalogs = ks.catalogs;
    // On its own only as the start-up update check asks: not under the launcher (it has just read the catalog), not
    // when the player turned the check off. Opening the menu asks anyway (show): the player is using it then.
    if (!env_set("RE_LAUNCHER") && (h.user_folder.empty() || read_update_prefs(h.user_folder).ask_at_start)) start_live();
  }
  // The signed catalog asked again for the kit's values (rekit/settings.h, the live layer) at most every
  // live_every_hours, on its own thread; the report page and the rest follow it without a release.
  std::thread live;
  std::atomic<bool> live_stop{false}, live_changed{false};
  bool report_set = false;  // set_report_url: the game's own address wins
  bool live_tried = false;
  void start_live() {
    const UpdateSource& u = h.update;
    if (live_tried) return;
    live_tried = true;
    if (kit_test() || u.catalogs.empty() || u.keys.empty() || u.recipe.key.empty() || h.user_folder.empty() ||
        !rekit::live_kit_settings_due(h.user_folder, static_cast<int64_t>(std::time(nullptr))))
      return;
    live = std::thread([this, catalogs = u.catalogs, keys = u.keys, key = u.recipe.key, user = h.user_folder, file = u.allow_file] {
      std::error_code ec;
      const fs::path dir = fs::temp_directory_path(ec) / ("rekit-live-" + key);
      fs::create_directories(dir, ec);
      std::string text, why;
      if (fetch_signed_catalog(catalogs, keys, dir.string(), file, [this](double) { return !live_stop.load(); }, &text, &why) &&
          !live_stop)
        rekit::apply_live_kit_settings(text, key, user, nullptr), live_changed = true;
      fs::remove_all(dir, ec);
    });
  }
  std::string report() const {
    if (!report_set) {
      const std::string live_url = rekit::kit_settings().report_url;
      if (!live_url.empty()) return live_url;
    }
    return report_url;
  }
  ~Impl() {
    work_stop = true, live_stop = true;
    if (worker.joinable()) worker.join();
    if (live.joinable()) live.join();
    for (SDL_AudioStream* a : audio)
      if (a) SDL_DestroyAudioStream(a);
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
    if (n >= hint_starts()) return;
    std::error_code ec;
    fs::create_directories(f.parent_path(), ec);
    std::ofstream(f) << (n + 1) << "\n";
    hint_on = true;
  }

  // --- Open and close.
  void show(int p) {
    start_live();  // the report page and the rest read live when the player opens the menu (at most every live_every_hours)
    if (open) return;
    release_game();
    open = true, page = p, confirm_change = false, confirm_reset = false, confirm_update = false, status.clear(), pending_entry = -1,
         hint_on = notice = false;
    hot = kNav + page, dirty = true;
    if (h.pause) h.pause();
    log("menu: open (" + std::string(kPageNames[page]) + ")");
  }
  void hide() {
    if (!open) return;
    if (prompt) log("menu: the update prompt: NOT NOW");
    open = false, prompt = false, confirm_update = false, dirty = true;
    unfinished.clear();
    if (checking && !checking->done) checking.reset();  // a check stops with the menu (the files are not read in play)
    if (work == kUpdating) work_stop = true;  // a download stops; an install already under way finishes
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
      else if (prompt_waits) show_prompt();
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
    if (hint_takes(e)) return true;
    track_game(e);
    if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) dirty = true;
    deliver(e);
    return false;
  }
  // The hint's ×: a click inside its own rectangle closes the hint (for this start; it fades out). Every other click,
  // the rest of the hint's included, is the game's, and none is taken while the game holds the mouse (relative mode).
  bool hint_takes(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && hint_x_button && e.button.button == hint_x_button) {
      hint_x_button = 0;
      return true;
    }
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || !hint_on || hint_closed || hint_x.w <= 0 || shown != kHint) return false;
    if (h.window && SDL_GetWindowRelativeMouseMode(h.window)) return false;
    const SDL_FPoint at{e.button.x, e.button.y};
    if (!SDL_PointInRectFloat(&at, &hint_x)) return false;
    hint_closed = std::max<uint64_t>(now(), hint_from + 1), hint_x_button = e.button.button;
    log("menu: the hint closed (its x)");
    return true;
  }
  // The hint's end and its alpha at `n`: in over hint_in_ms(), out over the hint_out_ms() before its end (10 s after its
  // first frame, or the ×'s click plus hint_out_ms()). By the clock, so the game's own rate does not change it.
  uint64_t hint_end() const {
    const uint64_t stays = notice ? notice_ms() : hint_ms();
    return hint_closed ? std::min(hint_closed + hint_out_ms(), hint_from + stays) : hint_from + stays;
  }
  uint8_t hint_alpha_at(uint64_t n) const {
    const uint64_t end = hint_end();
    if (n >= end) return 0;
    const float in = std::min(1.0f, static_cast<float>(n - hint_from) / hint_in_ms());
    const float out = std::min(1.0f, static_cast<float>(end - n) / hint_out_ms());
    return static_cast<uint8_t>(std::lround(255.0f * std::min(in, out)));
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
      page = id - kNav, confirm_change = false, confirm_reset = false, confirm_update = false, status.clear();
    if (id != kNone) play(hover, static_cast<float>(rekit::kit_settings().hover_volume));
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
  void into_page() {  // the highlight moves with no hover sound: it would cut the section's click off
    layout();
    for (const Button& b : buttons)
      if (b.col == 1) {
        hot = b.id, dirty = true;
        return;
      }
  }
  void back() {
    if (prompt) return hide();
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
          play(hover, static_cast<float>(rekit::kit_settings().hover_volume));
        }
      } else if (e.button.button == SDL_BUTTON_LEFT && under != kNone) {
        hot = under, dirty = true;
        press(under);
      }
    }
  }
  void back_to_nav() {
    if (prompt) return;
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

  bool entry_available(int i) const {
    const MenuEntry& en = h.entries[static_cast<size_t>(i)];
    return !en.available || en.available();
  }
  void press(int id) {
    if (id == kNone) return;
    if (!(id >= kEntry && id < kEntry + static_cast<int>(h.entries.size()) && !entry_available(id - kEntry))) play(click, static_cast<float>(rekit::kit_settings().click_volume));
    if (id >= kNav && id < kNav + kPages) {
      page = id - kNav, confirm_change = false, confirm_reset = false, confirm_update = false, status.clear(), dirty = true;
      into_page();
      if (!button(hot) || button(hot)->col != 1) hot = id;
      return;
    }
    if (id >= kEntry && id < kEntry + static_cast<int>(h.entries.size())) {
      const MenuEntry& en = h.entries[id - kEntry];
      if (!entry_available(id - kEntry)) {  // lit, so its note is read, but not picked
        log("menu: the game's " + upper(en.label) + " is not available" + (en.unavailable_note.empty() ? "" : ": " + en.unavailable_note));
        return;
      }
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
        log("menu: open the report page: " + report());
        if (!open_url(report())) say("Could not open " + report() + ".", true);
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
      default: return press_update(id);
    }
  }

  // --- UPDATES.
  std::string game_name() const { return h.title.empty() ? std::string("the game") : h.title; }
  std::string have() const {
    const std::string v = version_number(h.update.recipe.tag);
    return v.empty() ? h.version : v;
  }
  int badge_count() const { return upd.kind == UpdateCheck::kNewer ? std::max<int>(1, static_cast<int>(upd.notes.size())) : 0; }
  bool installable() const { return upd.kind == UpdateCheck::kNewer && !upd.sources.empty() && blocked.empty(); }
  void take_check(UpdateCheck c, std::string counts) {
    upd = std::move(c), upd_counts = std::move(counts), upd_checked = true, dirty = true;
    blocked = upd.kind == UpdateCheck::kNewer ? update_blocked(h.update, h.install) : std::string();
  }
  void show_notice() {
    prompt_waits = notice = true;
    hint_on = true, hint_started = false, hint_closed = 0, hint_x = {}, hint_alpha = 0, dirty = true;
    if (!unfinished.empty()) log("menu: the update notice: " + unfinished + " did not finish (this is " + have() + ")");
    else log("menu: the update notice: " + game_name() + " " + version_number(upd.tag) + " (" + keys_words() + ")");
  }
  void show_prompt() {
    prompt_waits = false;
    show(kUpdates);
    prompt = true, hot = unfinished.empty() || upd.kind == UpdateCheck::kNewer ? kPromptGo : kPromptLater, dirty = true;
    if (!unfinished.empty()) log("menu: the update did not finish: " + unfinished + " (this is " + have() + ")");
    else log("menu: the update prompt: " + game_name() + " " + version_number(upd.tag));
  }
  void quit_game() {
    if (h.quit) return h.quit();
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
  }
  void start_work(Work w) {
    if (work != kIdle) return;
    if (worker.joinable()) worker.join();
    work = w, work_stop = false, work_done = false, work_permille = 0, shown_permille = -1, dirty = true;
    work_error.clear(), work_install = UpdateInstall{};
    if (w == kChecking) {
      log("menu: CHECK NOW");
      worker = std::thread([this] {
        std::string counts;
        UpdateCheck c = check_update(h.update, &counts, [this] { return !work_stop.load(); });
        std::lock_guard<std::mutex> lock(work_m);
        work_check = std::move(c), work_counts = std::move(counts);
        work_done = true;
      });
      return;
    }
    log("menu: UPDATE NOW: " + version_number(upd.tag) + " (" + install_kind_name(h.install) + ")");
    worker = std::thread([this, check = upd, counts = upd_counts] {
      std::string error, file;
      bool github = false, cancelled = false;
      UpdateInstall r;
      const std::string dir = update_dir(h.update.temp, h.update.recipe.key, &error);
      const auto bar = [this](const Progress& p) {
        work_permille = static_cast<int>(p.fraction * 1000);
        return !work_stop.load();
      };
      if (!dir.empty() && download_update(h.update, check, dir, bar, &file, &github, &error, &cancelled))
        r = install_update(h.update, h.install, h.user_folder, file, github, check.tag, counts);
      else if (!cancelled) error = "The update could not be downloaded: " + error + ".";
      std::lock_guard<std::mutex> lock(work_m);
      work_install = std::move(r), work_error = std::move(error);
      work_done = true;
    });
  }
  void finish_work() {
    std::lock_guard<std::mutex> lock(work_m);
    const Work was = work;
    work = kIdle, dirty = true;
    if (was == kChecking) {
      take_check(work_check, work_counts);
      log("menu: checked: " + upd.words);  // the page says it
      return;
    }
    if (work_install.quit) {
      log("menu: updated: " + work_install.words + (work_install.backup.empty() ? "" : " (backup: " + work_install.backup + ")"));
      hide();
      return quit_game();
    }
    const std::string why = work_error.empty() ? work_install.words : work_error;
    log("menu: no update: " + why);
    confirm_update = false;
    if (open) say(why, true);
  }
  void poll_updates() {
    if (update_check && !update_check->running()) {
      UpdateCheck c;
      std::string counts, unf;
      if (update_check->take(&c, &counts, &unf)) {
        const bool fresh = (c.kind != UpdateCheck::kFailed || !c.words.empty()) && !upd_checked;
        if (fresh) take_check(c, counts), log("menu: start-up check: " + c.words);
        if (!open) unfinished = unf;  // once: the note is gone already
        if (!open && (!unfinished.empty() || (fresh && c.kind == UpdateCheck::kNewer))) show_notice();
      }
      update_check.reset();
    }
    if (work == kIdle) return;
    if (work_done) {
      if (worker.joinable()) worker.join();
      finish_work();
    } else if (const int pm = work_permille; pm != shown_permille) {
      shown_permille = pm, dirty = open;
    }
  }
  void press_update(int id) {
    switch (id) {
      case kUpdBox:
        prefs.ask_at_start = !prefs.ask_at_start, dirty = true;
        write_update_prefs(h.user_folder, prefs);
        log(std::string("menu: check at start: ") + (prefs.ask_at_start ? "on" : "off"));
        return;
      case kUpdCheckNow: return start_work(kChecking);
      case kUpdNow: confirm_update = true, dirty = true, hot = kUpdKeep, status.clear(); return;
      case kUpdKeep: confirm_update = false, dirty = true, hot = kUpdNow; return;
      case kUpdGo: hot = kUpdGo; return start_work(kUpdating);
      case kUpdPage:
        log("menu: open the release page: " + upd.page);
        if (!open_url(upd.page)) say("Could not open " + upd.page + ".", true);
        return;
      case kPromptGo:  // the page, asking to close the game
        log("menu: the update prompt: UPDATE NOW");
        prompt = false, confirm_update = true, hot = installable() ? kUpdGo : kUpdPage, dirty = true;
        unfinished.clear();
        return;
      case kPromptPage:
        log("menu: the update prompt: the release page " + upd.page);
        if (!open_url(upd.page)) say("Could not open " + upd.page + ".", true);
        return hide();
      case kPromptLater: return hide();
      case kPromptNever:
        log("menu: the update prompt: DON'T ASK AGAIN");
        prefs.ask_at_start = false;
        write_update_prefs(h.user_folder, prefs);
        prompt = false;
        return hide();
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
    const std::vector<LeaveOut> leave = h.leave_out, rewritten = h.rewritten;
    const auto own = h.check_game_files;
    log("menu: checking the files");
    c->th = std::thread([c, sets, leave, rewritten, own] {
      for (const auto& [label, root] : sets) {
        {
          std::lock_guard<std::mutex> g(c->m);
          c->which = label, c->n = 0, c->total = 0, c->path.clear();
        }
        const FilesProgress progress = [c](int n, int total, const std::string& path) {
          std::lock_guard<std::mutex> g(c->m);
          c->n = n, c->total = total, c->path = path;
          return !c->cancel.load();
        };
        Checking::Set one;
        one.label = label, one.r = check_files(root, leave, progress, rewritten);
        if (!one.r.has_manifest && own && label == "The game files" && !c->cancel) {
          one.by_game = true, one.problems = own();
          std::string why;
          if (one.problems.empty() && !c->cancel) one.recorded = write_manifest(root, leave, progress, &why);
        }
        std::lock_guard<std::mutex> g(c->m);
        c->results.push_back(std::move(one));
        if (c->cancel) break;
      }
      c->done = true;
    });
    hot = kCheckCancel, dirty = true;
  }
  // The check's words for one set.
  std::vector<std::pair<std::string, bool>> check_words(const Checking::Set& set) const {
    const std::string& label = set.label;
    const FilesCheck& r = set.r;
    std::vector<std::pair<std::string, bool>> out;  // a line, whether it is about a problem
    if (set.by_game) {
      if (set.problems.empty()) {
        out.push_back({label + ": as they should be, by the game's own check." +
                       std::string(set.recorded ? " A record of them is kept now for later checks." : ""), false});
        return out;
      }
      out.push_back({label + ": " + std::to_string(set.problems.size()) +
                     (set.problems.size() == 1 ? " problem" : " problems") + ", by the game's own check.", true});
      for (size_t i = 0; i < set.problems.size() && i < listed(); ++i) out.push_back({"    " + set.problems[i], true});
      if (set.problems.size() > listed())
        out.push_back({"    and " + std::to_string(set.problems.size() - listed()) + " more (the log lists them all)", true});
      return out;
    }
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
    size_t named = 0;
    for (const std::string& f : r.changed)
      if (named++ < listed()) out.push_back({"    " + f + " (changed)", true});
    for (const std::string& f : r.missing)
      if (named++ < listed()) out.push_back({"    " + f + " (missing)", true});
    if (bad > listed()) out.push_back({"    and " + std::to_string(bad - listed()) + " more (the log lists them all)", true});
    return out;
  }
  void finish_check() {
    if (!checking || !checking->done || checking->th.joinable() == false) return;
    checking->th.join();
    for (const auto& set : checking->results) {
      const std::string& label = set.label;
      const FilesCheck& r = set.r;
      std::string line = "menu: check: " + label + ": ";
      if (set.by_game) {
        log(line + "the game's own check, problems: " + std::to_string(set.problems.size()) +
            (set.recorded ? ", record written" : ""));
        for (const std::string& p : set.problems) log("menu: check: " + p);
        continue;
      }
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
    const bool is_hover = &s == &hover;
    if (h.play_sound) return h.play_sound(is_hover);
    if (!s.buf || kit_test() || !SDL_WasInit(SDL_INIT_AUDIO)) return;  // a test run is silent, the game's audio up or not
    if (!audio_tried) {
      audio_tried = true;
      for (int i = 0; i < 2; ++i) {
        const Sound& one = i ? hover : click;
        if (!one.buf) continue;
        audio[i] = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &one.spec, nullptr, nullptr);
        if (audio[i]) SDL_ResumeAudioStreamDevice(audio[i]);
      }
    }
    SDL_AudioStream* a = audio[is_hover ? 1 : 0];
    if (!a) return;
    SDL_SetAudioStreamGain(a, gain);
    SDL_ClearAudioStream(a);
    SDL_PutAudioStreamData(a, s.buf, static_cast<int>(s.len));
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
    if (prompt) return prompt_pass();
    const float px = 180, py = 70, pwid = 920, phei = 580;
    // The panel, its header band and the sections' column.
    fill({px, py, pwid, phei}, t.panel);
    fill({px, py, pwid, 72}, t.band_colour);
    edge({px, py, pwid, phei}, t.rule);
    fill({px + 1, py + 72, pwid - 2, 1}, t.rule);
    fill({px + 230, py + 73, 1, phei - 74}, t.rule);
    // The header: the badge, the game's name, SETUP · THE GAME IS PAUSED (NOT PAUSED when runs_on says so), RETURN TO GAME.
    float tx = px + 24;
    if (badge && drawing) {
      SDL_FRect d{tx + ox / k, py + 16 + oy / k, 40, 40};
      SDL_RenderTexture(ren, badge, nullptr, &d);
    }
    if (badge) tx += 54;
    words(bold, t.header.empty() ? upper(h.title) : t.header, 28, t.text, tx, py + 30);
    words(bold, runs_on_drawn ? "SETUP  \xC2\xB7  THE GAME IS NOT PAUSED" : "SETUP  \xC2\xB7  THE GAME IS PAUSED", 15, t.accent, tx, py + 54);
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
    const bool newer = page == kUpdates && upd.kind == UpdateCheck::kNewer;
    const std::string title = newer ? "UPDATE AVAILABLE: " + version_number(upd.tag) : std::string(kPageNames[page]);
    words(bold, title, 24, t.text, x, top + 14);
    if (newer) {
      const uint64_t size = upd.sources.empty() ? 0 : upd.sources[0].file.size;
      words(body, "You have " + have() + (size ? "  \xC2\xB7  " + megabytes(size) : ""), 17, t.dim,
            x + static_cast<float>(bold.width(title, 24)) + 18, top + 16);
    }
    top += 44;
    switch (page) {
      case kFiles: files_page(x, top, w); break;
      case kUpdates: updates_page(x, top, w, py + phei - 48); break;
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
  // The checkbox row: "Check for updates when <game> starts" and CHECK NOW, its top `y`; its height.
  float check_row(float x, float y, float w) {
    fill({x, y, w, 1}, t.rule), fill({x, y + 59, w, 1}, t.rule);
    const std::string now = work == kChecking ? "CHECKING..." : "CHECK NOW";
    const float nw = static_cast<float>(bold.width(now, 17)) + 36;
    add(kUpdBox, {x, y + 8, w - nw - 16, 44}, "Check for updates when " + game_name() + " starts", 1);
    page_button(kUpdCheckNow, now, x + w - nw, y + 8);
    return 60;
  }
  void updates_page(float x, float top, float w, float bottom) {
    if (!h.update.usable()) {
      para(body, "This copy has no update source. New versions of " + game_name() + " are on its release page.", 17, t.dim, x, top, w);
      return;
    }
    if (upd.kind != UpdateCheck::kNewer) {
      const std::string said = work == kChecking ? "Checking for updates..." : !upd_checked ? "Not checked yet." : upd.words;
      top += para(body, said, 17, upd.kind == UpdateCheck::kFailed && upd_checked ? t.bad : t.dim, x, top, w) + 16;
      check_row(x, top, w);
      return;
    }
    // From the bottom up: UPDATE NOW's row, the backup line, the checkbox row; the notes fill what is above.
    const float row = bottom - 50, line = row - 14 - 44, box = line - 12 - 60;
    words(bold, "WHAT'S NEW", 15, t.accent, x, top + 9);
    fill({x, top + 22, w, 1}, t.rule);
    float y = top + 32;
    const float skip = static_cast<float>(body.line_skip(17));
    bool cut = false;
    for (const auto& [tag, notes] : upd.notes) {
      if (y + 28 + skip > box - 8) {  // a version's name only with a line of its notes under it
        cut = true;
        break;
      }
      words(bold, version_number(tag), 18, t.text, x, y + 12), y += 28;
      for (const auto& [heading, text] : note_lines(notes)) {
        const auto lines = (heading ? bold : body).wrap(text, 17, static_cast<int>(w));
        if (y + skip * static_cast<float>(lines.size()) > box - 8) {
          cut = true;
          break;
        }
        y += para(heading ? bold : body, text, 17, t.text, x, y, w) + 4;
      }
      if (cut) break;
      y += 8;
    }
    if (cut && !upd.page.empty()) words(body, "More on the release page.", 15, t.dim, x, box - 14);
    check_row(x, box, w);
    // The backup line, or the question before the game closes.
    const bool cant = !installable();
    if (confirm_update && !cant) {
      para(body, "Close " + game_name() + " and update? Anything not saved is lost.", 17, t.bad, x, line + 10, w);
    } else if (!cant && (!h.update.keep.empty() || h.update.keep_now)) {
      fill({x, line, w, 44}, t.well), edge({x, line, w, 44}, t.rule);
      words(body, body.fit("Your saves and settings are backed up first, to Backups/" + today() + " in your data folder.", 16,
                           static_cast<int>(w - 28)), 16, t.good, x + 14, line + 22);
    }
    // UPDATE NOW, or the release page when this copy can't take it.
    if (cant) {
      const std::string why = !blocked.empty() ? blocked : upd.words;
      if (upd.page.empty()) return void(para(body, why, 16, t.dim, x, row, w));
      const float r = page_button(kUpdPage, "OPEN THE RELEASE PAGE", x, row + 2, true);
      const float tw = x + w - r - 14;
      const float th = static_cast<float>(body.line_skip(15) * static_cast<int>(body.wrap(why, 15, static_cast<int>(tw)).size()));
      para(body, why, 15, t.dim, r + 14, row + 2 + std::max(0.0f, (48 - th) / 2), tw);
    } else if (work == kUpdating) {
      const float f = static_cast<float>(work_permille.load()) / 1000.0f;
      fill({x, row + 18, 300, 12}, t.well), edge({x, row + 18, 300, 12}, t.rule), fill({x + 1, row + 19, 298 * f, 10}, t.accent);
      words(body, f < 1 ? "Downloading the update... " + std::to_string(static_cast<int>(f * 100)) + "%" : "Putting the update in place...",
            16, t.dim, x + 316, row + 24);
    } else if (confirm_update) {
      const float r = page_button(kUpdGo, "CLOSE AND UPDATE", x, row, true);
      page_button(kUpdKeep, "KEEP PLAYING", r + 12, row + 2);
    } else {
      const float r = page_button(kUpdNow, "UPDATE NOW", x, row, true);
      words(body, "Asks before closing the game. It restarts when the update is in.", 16, t.dim, r + 14, row + 24);
    }
  }
  // The start-up prompt (plan section 4): a box over the game's own start, the menu's look.
  void prompt_pass() {
    const float bx = 340, by = 200, bw = 600, ix = bx + 28, iw = bw - 56;
    const uint64_t size = upd.sources.empty() ? 0 : upd.sources[0].file.size;
    const std::string said = "You have " + have() + "." + (size ? " The update is " + megabytes(size) + "." : "") +
                             (h.update.keep.empty() && !h.update.keep_now ? "" : " Your saves and settings are backed up first.") +
                             " See what's new under UPDATES in the setup menu.";
    const bool newer = upd.kind == UpdateCheck::kNewer;
    const auto inside = [&]() {
      float y = by + 24;
      words(bold, upper(game_name()), 15, t.accent, ix, y + 9), y += 32;
      if (!unfinished.empty()) {  // an earlier UPDATE NOW did not get there (a declined Windows prompt): said once
        y += para(bold, "The update to " + game_name() + " " + unfinished + " didn't finish.", 28, t.text, ix, y, iw) + 10;
        y += para(body, "This is still " + have() + ", and it works as before." +
                            (newer ? " Try again, or get it from its release page." : " Try again from UPDATES in the setup menu."),
                  17, t.text, ix, y, iw) + 18;
        float r = ix - 12;
        if (newer) r = page_button(kPromptGo, "UPDATE NOW", ix, y, true);
        if (newer && !upd.page.empty()) r = page_button(kPromptPage, "RELEASE PAGE", r + 12, y + 2);
        page_button(kPromptLater, newer ? "NOT NOW" : "OK", r + 12, newer ? y + 2 : y, !newer);
        return y + 48 + 24 - by;
      }
      y += para(bold, game_name() + " " + version_number(upd.tag) + " is available.", 28, t.text, ix, y, iw) + 10;
      y += para(body, said, 17, t.text, ix, y, iw) + 18;
      float r = page_button(kPromptGo, "UPDATE NOW", ix, y, true);
      r = page_button(kPromptLater, "NOT NOW", r + 12, y + 2);
      page_button(kPromptNever, "DON'T ASK AGAIN", r + 12, y + 2);
      return y + 48 + 24 - by;
    };
    const bool was = drawing;
    drawing = false;
    const float hh = inside();  // measured, then drawn on its box
    buttons.clear();
    drawing = was;
    fill({bx, by, bw, hh}, t.panel), edge({bx, by, bw, hh}, t.rule);
    inside();
    for (const Button& b : buttons) draw_button(b);
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
      if (report().empty()) return y - it + para(body, "No page for reports is known. You have " + you + ".", 16, t.dim, ix, y, iw);
      y += para(body, "Opens the page where problems are reported, in your browser. Say you have " + you + ".", 16, t.dim, ix,
                y, iw) + 10;
      const float r = page_button(kOpenReport, "OPEN THE REPORT PAGE", ix, y, true);
      words(body, body.fit(report(), 15, static_cast<int>(ix + iw - r - 14)), 15, t.dim, r + 14, y + 24);
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
    for (const auto& set : checking->results) {
      const FilesCheck& r = set.r;
      for (const auto& [line, bad] : check_words(set)) {
        const bool item = line.rfind("    ", 0) == 0;  // a file named under its set, indented
        top += para(body, item ? line.substr(4) : line, 16, bad ? t.bad : t.good, item ? x + 20 : x, top, item ? w - 20 : w) + 2;
      }
      top += 8;
      if ((r.has_manifest && !r.ok() && !r.cancelled) || !set.problems.empty())
        (set.label == "The game files" ? game_damage : program_damage) = true;
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
    if (b.id == kUpdBox) {  // a checkbox and its words
      const SDL_FRect box{b.r.x + 2, b.r.y + b.r.h / 2 - 10, 20, 20};
      fill(box, t.well), edge(box, lit ? t.accent : t.plate_edge);
      if (prefs.ask_at_start) fill({box.x + 5, box.y + 5, 10, 10}, t.accent);
      words(bold, b.label, 18, lit ? t.accent : t.text, b.r.x + 34, b.r.y + b.r.h / 2);
      return;
    }
    if (b.id >= kNav && b.id < kEntry + 100 && b.id != kReturn) {  // a section or an entry: a row of the column
      const bool current = b.id == kNav + page;
      if (current) fill(b.r, t.nav_on), fill({b.r.x, b.r.y, 3, b.r.h}, t.accent);
      else if (lit) fill(b.r, t.nav_on);
      const int entry = b.id - kEntry;
      if (entry >= 0 && entry < static_cast<int>(h.entries.size()) && !entry_available(entry)) {  // dimmed, its note below
        const SDL_Color faint{t.dim.r, t.dim.g, t.dim.b, static_cast<Uint8>(t.dim.a * 3 / 5)};
        std::string note = h.entries[static_cast<size_t>(entry)].unavailable_note;
        const int room = static_cast<int>(b.r.w) - 21 - 12;
        while (!note.empty() && body.width(note + "...", 13) > room && body.width(note, 13) > room) note.pop_back();
        if (note != h.entries[static_cast<size_t>(entry)].unavailable_note) note += "...";
        words(bold, b.label, 18, faint, b.r.x + 21, b.r.y + (note.empty() ? b.r.h / 2 : 15));
        if (!note.empty()) words(body, note, 13, faint, b.r.x + 21, b.r.y + 32);
        return;
      }
      words(bold, b.label, 18, current ? t.text : lit ? t.accent : t.dim, b.r.x + 21, b.r.y + b.r.h / 2);
      if (b.id == kNav + static_cast<int>(kUpdates) && badge_count() > 0) {  // the number of newer versions, a pill after the name
        const std::string n = std::to_string(badge_count());
        const float pw = static_cast<float>(bold.width(n, 13)) + 14, px = b.r.x + 21 + static_cast<float>(bold.width(b.label, 18)) + 10;
        fill({px, b.r.y + b.r.h / 2 - 9, pw, 18}, t.accent);
        words(bold, n, 13, t.on_accent, px + pw / 2, b.r.y + b.r.h / 2, 0);
      }
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
    // "SETUP keys" and a small × at the top right, whose hit area is 26 px square however small it is drawn; the
    // start-up check's find in its place, "UPDATE AVAILABLE 1.4.0 · press keys".
    const std::string a = !notice ? "SETUP" : unfinished.empty() ? "UPDATE AVAILABLE" : "UPDATE DIDN'T FINISH";
    const std::string b = !notice ? keys_words()
                          : unfinished.empty() ? version_number(upd.tag) + " \xC2\xB7 press " + keys_words()
                                               : "press " + keys_words();
    const int size = 21;
    const float aw = static_cast<float>(bold.width(a, size)), bw = static_cast<float>(body.width(b, size)), hh = 39, xs = 26;
    const SDL_FRect box{20, lh - 16 - hh, 14 + aw + 11 + bw + 10 + xs + 2, hh};
    hint_x = {box.x + box.w - xs - 2, box.y + 2, xs, xs};
    SDL_SetRenderDrawColor(ren, 8, 11, 16, 158);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    const SDL_Color e{t.accent.r, t.accent.g, t.accent.b, 115};
    SDL_SetRenderDrawColor(ren, e.r, e.g, e.b, e.a);
    SDL_RenderRect(ren, &box);
    bold.draw(ren, a, size, t.accent, static_cast<int>(box.x + 14), static_cast<int>(box.y + hh / 2), -1);
    body.draw(ren, b, size, t.dim, static_cast<int>(box.x + 14 + aw + 11), static_cast<int>(box.y + hh / 2), -1);
    const float cx = hint_x.x + xs / 2, cy = hint_x.y + xs / 2, r = 4.5f;
    SDL_SetRenderDrawColor(ren, t.dim.r, t.dim.g, t.dim.b, 255);
    for (const float d : {-0.5f, 0.0f, 0.5f})  // a little over a pixel thick
      SDL_RenderLine(ren, cx - r + d, cy - r, cx + r + d, cy + r), SDL_RenderLine(ren, cx - r + d, cy + r, cx + r + d, cy - r);
    SDL_RenderPresent(ren);
    // The fade: the box's alpha scaled (the picture is straight RGBA, the rest of it clear).
    if (hint_alpha == 255) return;
    const int x0 = std::max(0, static_cast<int>(box.x * s) - 1), y0 = std::max(0, static_cast<int>(box.y * s) - 1);
    const int x1 = std::min(surf->w, static_cast<int>((box.x + box.w) * s) + 2), y1 = std::min(surf->h, static_cast<int>((box.y + box.h) * s) + 2);
    for (int y = y0; y < y1; ++y) {
      uint8_t* row = static_cast<uint8_t*>(surf->pixels) + static_cast<size_t>(y) * surf->pitch;
      for (int x = x0; x < x1; ++x) row[x * 4 + 3] = static_cast<uint8_t>((row[x * 4 + 3] * hint_alpha + 127) / 255);
    }
  }

  void frame() {
    if (live_changed.exchange(false)) dirty = true;  // the live values read: the report page may show another address
    poll_updates();
    const uint64_t n = now();
    if (hint_on && !hint_started) hint_started = true, hint_from = n;
    if (hint_on && n >= hint_end()) hint_on = notice = false, hint_x = {};
    if (hint_on && hint_alpha_at(n) != hint_alpha) hint_alpha = hint_alpha_at(n), dirty = true;  // a new picture only while fading
    if (checking && checking->done && checking->th.joinable()) finish_check();
    if (checking && !checking->done && open && page == kCheck && n - check_drawn_at >= 200) {
      std::lock_guard<std::mutex> g(checking->m);
      if (checking->n != check_drawn) dirty = true, check_drawn_at = n;  // the bar, five times a second at most
    }
    if (open && !h.entries.empty()) {  // each entry's available(), asked at each frame, drawn when one changes
      entries_drawn.resize(h.entries.size(), true);
      for (size_t i = 0; i < h.entries.size(); ++i)
        if (const bool a = entry_available(static_cast<int>(i)); a != entries_drawn[i]) entries_drawn[i] = a, dirty = true;
    }
    if (open && h.runs_on) {  // the header's PAUSED / NOT PAUSED, asked at each frame, drawn when it changes
      const bool on = h.runs_on();
      if (on != runs_on_drawn) runs_on_drawn = on, dirty = true;
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
  if (m.h.update.usable() && !m.h.user_folder.empty()) {
    m.prefs = read_update_prefs(m.h.user_folder);
    m.update_check = std::make_unique<StartUpdateCheck>(m.h.update, m.h.user_folder);
  }
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
  impl_->report_url = url, impl_->report_set = true, impl_->dirty = true;
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
