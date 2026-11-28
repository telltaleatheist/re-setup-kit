#include "rekit/sdl/game_menu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

#include "rekit/sdl/setup_original.h"
#include "setup_text.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#endif

namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

// The menu is laid out on a 1280x720 frame (the mockup's), scaled to fit the window and centred; the scrim covers it
// all. The hint is in the window's own logical pixels.
constexpr float kW = 1280, kH = 720;
constexpr int kHintStarts = 3;          // the first few starts show the hint
constexpr uint64_t kHintMs = 5000;      // for the first 5 seconds of each
constexpr uint64_t kHoldMs = 1000;      // a combination modifier held alone goes on to the game after this

enum Page { kFiles, kUpdates, kReport, kData };
constexpr int kPages = 4;
const char* const kPageNames[kPages] = {"GAME FILES", "UPDATES", "REPORT A PROBLEM", "OPEN MY DATA FOLDER"};

enum Id {
  kNone = 0,
  kReturn = 1,
  kChange = 10, kChangeGo, kChangeKeep,
  kOpenLog = 20, kCopyLog, kOpenReport,
  kOpenData = 30, kCopyData,
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
bool is_combo_modifier(SDL_Keycode k) {
#if defined(__APPLE__)
  return k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LGUI || k == SDLK_RGUI;
#else
  return k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LALT || k == SDLK_RALT;
#endif
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
  // The keys: combination modifiers held back from the game, those down, the ups the game never gets.
  std::vector<std::pair<SDL_Event, uint64_t>> held;
  std::set<SDL_Scancode> mods_down, mods_game_saw, swallow_up;
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
    open = true, page = p, confirm_change = false, status.clear(), pending_entry = -1, hint_on = false;
    hot = kNav + page, dirty = true;
    if (h.pause) h.pause();
    log("menu: open (" + std::string(kPageNames[page]) + ")");
  }
  void hide() {
    if (!open) return;
    open = false, dirty = true;
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
  void flush_held() {
    auto list = std::move(held);
    held.clear();
    for (auto& [ev, at] : list) mods_game_saw.insert(ev.key.scancode), deliver(ev);
  }
  bool combo(const SDL_Event& e) const {
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.key != SDLK_F1 || e.key.repeat) return false;
    const SDL_Keymod m = e.key.mod;
#if defined(__APPLE__)
    return (m & SDL_KMOD_GUI) && (m & SDL_KMOD_SHIFT);
#else
    return (m & SDL_KMOD_ALT) && (m & SDL_KMOD_SHIFT);
#endif
  }
  void take_combo(const SDL_Event& e) {
    held.clear();
    for (const SDL_Scancode s : mods_game_saw) {  // a modifier the game saw go down goes up for it now
      SDL_Event up{};
      up.type = SDL_EVENT_KEY_UP;
      up.key = e.key;
      up.key.type = SDL_EVENT_KEY_UP, up.key.scancode = s, up.key.key = SDL_GetKeyFromScancode(s, SDL_KMOD_NONE, false);
      up.key.down = false, up.key.repeat = false, up.key.mod = SDL_KMOD_NONE;
      deliver(up);
    }
    mods_game_saw.clear();
    swallow_up = mods_down;
    swallow_up.insert(e.key.scancode);
    if (open) hide();
    else show(kFiles);
  }
  bool on_event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN && is_combo_modifier(e.key.key)) mods_down.insert(e.key.scancode);
    if (e.type == SDL_EVENT_KEY_UP && is_combo_modifier(e.key.key)) mods_down.erase(e.key.scancode);
    if (e.type == SDL_EVENT_KEY_UP && swallow_up.erase(e.key.scancode)) return true;
    if (keys_on && combo(e)) {
      take_combo(e);
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
      menu_input(e);
      return true;
    }
    if (keys_on && !held.empty() && !(e.type == SDL_EVENT_KEY_DOWN && is_combo_modifier(e.key.key)))
      flush_held();  // anything but another modifier of the combination: the held ones were the game's
    if (keys_on && e.type == SDL_EVENT_KEY_DOWN && is_combo_modifier(e.key.key) && !mods_game_saw.count(e.key.scancode)) {
      held.push_back({e, now()});
      return true;
    }
    if (e.type == SDL_EVENT_KEY_UP) mods_game_saw.erase(e.key.scancode);
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
    if (id >= kNav && id < kNav + kPages && page != id - kNav) page = id - kNav, confirm_change = false, status.clear();
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
          if (!e.key.repeat) swallow_up.insert(e.key.scancode), hide();
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
      page = id - kNav, confirm_change = false, status.clear(), dirty = true;
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
      case kOpenData: return open_folder(h.user_folder, "the data folder");
      case kCopyData: return copy(h.user_folder);
      default: return;
    }
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
      case kData: data_page(x, top, w); break;
    }
    if (!status.empty()) para(body, status, 16, status_bad ? t.bad : t.good, x, py + phei - 40, w);
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
    if (!held.empty() && n - held.front().second >= kHoldMs) flush_held();
    if (hint_on && hint_until == 0) hint_until = n + kHintMs;
    if (hint_on && n >= hint_until) hint_on = false;
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
  std::string self;
  if (const char* app = SDL_getenv("APPIMAGE"); app && *app) self = app;  // an AppImage starts as itself
#if defined(_WIN32)
  if (self.empty()) {
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) {
      const std::u8string u = fs::path(std::wstring(buf, n)).u8string();  // UTF-8, as SDL takes paths
      self.assign(u.begin(), u.end());
    }
  }
#elif defined(__APPLE__)
  if (self.empty()) {
    char buf[PATH_MAX];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) == 0) self = fs::weakly_canonical(buf).string();
  }
#else
  if (self.empty()) {
    std::error_code ec;
    self = fs::read_symlink("/proc/self/exe", ec).string();
  }
#endif
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
