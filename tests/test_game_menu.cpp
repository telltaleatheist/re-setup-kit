// The in-game menu (game_menu.h) in a test host: a hidden window on SDL's dummy driver with a software renderer, which
// draws a "game" frame, hands its events to the menu and draws the menu's picture over the frame, as a game does.
// REKIT_MENU_SHOTS=<dir> saves each page drawn over the frame there (menu-*.bmp).
#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/original.h"
#include "rekit/sdl/game_menu.h"

namespace fs = std::filesystem;
using rekit::sdl::GameMenu;
using rekit::sdl::MenuEntry;
using rekit::sdl::MenuHooks;
using rekit::sdl::MenuImage;

namespace {

std::string tmp(const std::string& name) { return (fs::path(REKIT_TEST_TMP) / "game-menu" / name).string(); }

struct Env {  // SDL's copy of the environment (the kit reads it there), put back as it was after
  std::vector<std::pair<std::string, std::string>> was;
  std::vector<std::string> unset;
  void set(const char* name, const char* value) {
    if (const char* v = SDL_getenv(name)) was.push_back({name, v});
    else unset.push_back(name);
    if (value) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
    else SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name);
  }
  ~Env() {
    for (const auto& [n, v] : was) SDL_SetEnvironmentVariable(SDL_GetEnvironment(), n.c_str(), v.c_str(), true);
    for (const std::string& n : unset) SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), n.c_str());
  }
};

// The game: its window and renderer, what reached it, its clock's pauses, what the menu asked of it.
struct Host {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;
  uint64_t serial = 0, now = 0;
  int draws = 0, uploads = 0;
  std::vector<std::string> said;  // "pause", "resume", "pick", "restart", "open <url>", "copy <text>"
  std::vector<SDL_Event> got;     // the events the game was handed
  std::vector<std::string> log;
  Host() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    win = SDL_CreateWindow("game", 1280, 720, SDL_WINDOW_HIDDEN);
    ren = SDL_CreateRenderer(win, SDL_SOFTWARE_RENDERER);
  }
  ~Host() {
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
  }
  MenuHooks hooks() {
    MenuHooks h;
    h.window = win;
    h.draw = [this](const MenuImage& im) {
      ++draws;
      if (!tex) {
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, im.w, im.h);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      }
      if (im.serial != serial) SDL_UpdateTexture(tex, nullptr, im.pixels, im.pitch), serial = im.serial, ++uploads;
      SDL_RenderTexture(ren, tex, nullptr, nullptr);
    };
    h.deliver = [this](const SDL_Event& e) { got.push_back(e); };
    h.pause = [this] { said.push_back("pause"); };
    h.resume = [this] { said.push_back("resume"); };
    h.title = "Demo Game";
    h.version = "1.2.3";
    h.user_folder = tmp("user");
    h.log_file = tmp("user") + "/demo.log";
    h.files_in_use = tmp("user") + "/install";
    h.report_url = "https://github.com/example/demo/issues";
    h.install = rekit::InstallKind::kFolder;
    h.restart_for_setup = [this] { said.push_back("restart"); };
    h.log = [this](const std::string& l) { log.push_back(l); };
    h.open_url = [this](const std::string& u) { return said.push_back("open " + u), true; };
    h.copy_text = [this](const std::string& t) { return said.push_back("copy " + t), true; };
    h.now_ms = [this] { return now; };
    return h;
  }
  // The game's picture (a sky and a ground), then the menu over it.
  void game_and_menu(GameMenu& m) {
    SDL_SetRenderDrawColor(ren, 72, 82, 96, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 107, 102, 86, 255);
    const SDL_FRect ground{0, 280, 1280, 440};
    SDL_RenderFillRect(ren, &ground);
    m.frame();
  }
  void frame(GameMenu& m) {
    game_and_menu(m);
    SDL_RenderPresent(ren);
  }
  SDL_Color pixel(int x, int y) {
    SDL_Color c{};
    if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
      SDL_ReadSurfacePixel(s, x, y, &c.r, &c.g, &c.b, &c.a);
      SDL_DestroySurface(s);
    }
    return c;
  }
  void shot(GameMenu& m, const char* name) {
    const char* dir = SDL_getenv("REKIT_MENU_SHOTS");
    if (!dir || !*dir) return;
    game_and_menu(m);  // the frame read back before it is shown
    if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
      SDL_SaveBMP(s, (fs::path(dir) / (std::string("menu-") + name + ".bmp")).string().c_str());
      SDL_DestroySurface(s);
    }
    SDL_RenderPresent(ren);
  }
  bool has(const std::string& s) const {
    for (const std::string& l : said)
      if (l == s) return true;
    return false;
  }
  bool logged(const std::string& s) const {
    for (const std::string& l : log)
      if (l.find(s) != std::string::npos) return true;
    return false;
  }
};

SDL_Event key(bool down, SDL_Keycode k, SDL_Scancode sc, SDL_Keymod mod) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.type = static_cast<SDL_EventType>(e.type);
  e.key.key = k, e.key.scancode = sc, e.key.mod = mod, e.key.down = down;
  return e;
}
// The combination's second modifier on this system: Cmd on the Mac, Alt elsewhere.
#if defined(__APPLE__)
constexpr SDL_Keycode kMod2 = SDLK_LGUI;
constexpr SDL_Scancode kMod2Sc = SDL_SCANCODE_LGUI;
constexpr SDL_Keymod kMod2Bit = SDL_KMOD_LGUI;
#else
constexpr SDL_Keycode kMod2 = SDLK_LALT;
constexpr SDL_Scancode kMod2Sc = SDL_SCANCODE_LALT;
constexpr SDL_Keymod kMod2Bit = SDL_KMOD_LALT;
#endif
// The whole combination, down then up, through the menu.
void press_keys(GameMenu& m) {
  m.event(key(true, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT));
  m.event(key(true, kMod2, kMod2Sc, SDL_KMOD_LSHIFT | kMod2Bit));
  m.event(key(true, SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT | kMod2Bit));
  m.event(key(false, SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT | kMod2Bit));
  m.event(key(false, kMod2, kMod2Sc, SDL_KMOD_LSHIFT));
  m.event(key(false, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE));
}
// Every key, mouse button and pad button that went down for the game went up for it too (repeats aside).
bool balanced(const std::vector<SDL_Event>& got) {
  std::map<std::pair<Uint32, int>, int> held;
  for (const SDL_Event& e : got) {
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) ++held[{0, e.key.scancode}];
    if (e.type == SDL_EVENT_KEY_UP) --held[{0, e.key.scancode}];
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ++held[{1, e.button.button}];
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) --held[{1, e.button.button}];
  }
  for (const auto& [k, n] : held)
    if (n != 0) return false;
  return true;
}
bool has_key(const std::vector<SDL_Event>& got, Uint32 type, SDL_Scancode sc) {
  for (const SDL_Event& e : got)
    if (e.type == type && e.key.scancode == sc) return true;
  return false;
}
void tap(GameMenu& m, SDL_Keycode k, SDL_Scancode sc) {
  m.event(key(true, k, sc, SDL_KMOD_NONE));
  m.event(key(false, k, sc, SDL_KMOD_NONE));
}
void down(GameMenu& m, int n = 1) {
  for (int i = 0; i < n; ++i) tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN);
}
void up(GameMenu& m) { tap(m, SDLK_UP, SDL_SCANCODE_UP); }
void enter(GameMenu& m) { tap(m, SDLK_RETURN, SDL_SCANCODE_RETURN); }
void right(GameMenu& m) { tap(m, SDLK_RIGHT, SDL_SCANCODE_RIGHT); }

}  // namespace

// The keys open it with the game paused: F1 never reaches the game; Shift and Alt (Cmd) do, at once, and go up for it as
// the menu opens, their real ups then the kit's. Esc closes it back to the game, and its key-up stays the kit's too.
TEST(game_menu_keys_open_it_and_never_reach_the_game) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  GameMenu m(host.hooks(), {});
  EXPECT(!m.event(key(true, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT)) && host.got.size() == 1);  // at once
  m.event(key(true, kMod2, kMod2Sc, SDL_KMOD_LSHIFT | kMod2Bit));
  EXPECT(m.event(key(true, SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT | kMod2Bit)));
  EXPECT(m.is_open() && host.said == std::vector<std::string>{"pause"});
  EXPECT(host.got.size() == 4 && host.got[2].type == SDL_EVENT_KEY_UP && host.got[3].type == SDL_EVENT_KEY_UP && balanced(host.got));
  m.event(key(false, SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT | kMod2Bit));
  m.event(key(false, kMod2, kMod2Sc, SDL_KMOD_LSHIFT));
  m.event(key(false, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE));
  EXPECT(host.got.size() == 4 && !has_key(host.got, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F1));
  host.frame(m);
  EXPECT(host.draws == 1);
  tap(m, SDLK_A, SDL_SCANCODE_A);  // the open menu keeps the game's keys
  EXPECT(host.got.size() == 4);
  tap(m, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
  EXPECT(!m.is_open() && host.got.size() == 4 && host.said == (std::vector<std::string>{"pause", "resume"}));
  host.frame(m);
  EXPECT(host.draws == 1);  // nothing over the game now
  tap(m, SDLK_A, SDL_SCANCODE_A);
  EXPECT(host.got.size() == 6 && host.got[4].key.key == SDLK_A);
  // Opened again, the keys close it too, their ups the kit's; window events still reach the game meanwhile.
  host.got.clear();
  press_keys(m);
  SDL_Event moved{};
  moved.type = SDL_EVENT_WINDOW_MOVED;
  EXPECT(m.is_open() && host.got.size() == 4 && !m.event(moved) && host.got.size() == 5);
  press_keys(m);
  EXPECT(!m.is_open() && host.got.size() == 5 && balanced(host.got) && host.said.back() == "resume");
}

// A Shift or Alt (Cmd) alone is the game's at once, its repeats too. A key and a mouse button the game holds as the
// menu opens go up for it then; their repeats and real ups after are the kit's. A modifier's repeat while the menu is
// open, and after it closes, never reaches the game.
TEST(game_menu_a_lone_modifier_reaches_the_game_at_once) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  GameMenu m(host.hooks(), {});
  SDL_Event rep = key(true, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT);
  EXPECT(!m.event(rep) && host.got.size() == 1);
  rep.key.repeat = true;
  EXPECT(!m.event(rep) && host.got.size() == 2 && host.got[1].key.repeat);
  m.event(key(false, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE));
  EXPECT(host.got.size() == 3 && balanced(host.got));
  // W and the left button held as the game's button opens the menu.
  host.got.clear();
  m.event(key(true, SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE));
  SDL_Event click{};
  click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  click.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN, click.button.button = SDL_BUTTON_LEFT, click.button.down = true;
  m.event(click);
  m.show();
  EXPECT(host.got.size() == 4 && balanced(host.got));
  m.hide();
  SDL_Event w = key(true, SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE);
  w.key.repeat = true;
  EXPECT(m.event(w));
  EXPECT(m.event(key(false, SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE)));
  click.type = SDL_EVENT_MOUSE_BUTTON_UP, click.button.type = SDL_EVENT_MOUSE_BUTTON_UP, click.button.down = false;
  EXPECT(m.event(click) && host.got.size() == 4);
  // A modifier held through the keys: its repeats while open and after closing stay the kit's.
  host.got.clear();
  m.event(key(true, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT));
  m.event(key(true, kMod2, kMod2Sc, SDL_KMOD_LSHIFT | kMod2Bit));
  m.event(key(true, SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT | kMod2Bit));
  EXPECT(m.is_open() && m.event(rep) && host.got.size() == 4);
  tap(m, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
  EXPECT(!m.is_open() && m.event(rep) && host.got.size() == 4);
  m.event(key(false, SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT | kMod2Bit));
  m.event(key(false, kMod2, kMod2Sc, SDL_KMOD_LSHIFT));
  m.event(key(false, SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE));
  EXPECT(host.got.size() == 4 && balanced(host.got));
  // Then the keys are the game's again.
  rep.key.repeat = false;
  EXPECT(!m.event(rep) && host.got.size() == 5);
}

// RE_KIT_TEST=1: no keys (F1 with them is the game's), no hint, no count kept; the game's own button still opens it.
TEST(game_menu_keys_off_under_test_mode) {
  Env env;
  env.set("RE_KIT_TEST", "1"), env.set("RE_LAUNCHER", nullptr);
  std::error_code ec;
  fs::remove_all(tmp("user"), ec);
  Host host;
  GameMenu m(host.hooks(), {});
  press_keys(m);
  EXPECT(!m.is_open() && host.got.size() == 6);
  host.frame(m);
  EXPECT(host.draws == 0 && !fs::exists(tmp("user") + "/setup-hint.txt") && host.logged("keys off"));
  m.show();
  EXPECT(m.is_open());
}

// The hint: on the first three starts, for 5 seconds of each; never when the launcher started the game.
TEST(game_menu_hint_on_the_first_starts) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", nullptr);
  std::error_code ec;
  fs::remove_all(tmp("user"), ec);
  Host host;
  for (int start = 1; start <= 4; ++start) {
    host.now = 1000;
    GameMenu m(host.hooks(), {});
    if (start == 1) host.shot(m, "hint");
    host.draws = 0;
    host.frame(m);
    host.now = 5999, host.frame(m);
    const int shown = host.draws;
    host.now = 6000, host.frame(m);
    EXPECT(shown == (start <= 3 ? 2 : 0) && host.draws == shown);
  }
  int n = 0;
  std::ifstream(tmp("user") + "/setup-hint.txt") >> n;
  EXPECT(n == 3);
  fs::remove_all(tmp("user"), ec);
  env.set("RE_LAUNCHER", "2.2.0");
  host.draws = 0, host.now = 0;
  GameMenu m(host.hooks(), {});
  host.frame(m);
  EXPECT(host.draws == 0 && !fs::exists(tmp("user") + "/setup-hint.txt"));
}

// The game's own entry: shown after the kit's sections; picking it closes the menu, resumes the game, then calls it.
TEST(game_menu_game_entry_shows_picks_and_resumes) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  MenuHooks h = host.hooks();
  h.entries.push_back(MenuEntry{"Upgrades", [&host] { host.said.push_back("pick"); }});
  GameMenu m(std::move(h), {});
  press_keys(m);
  down(m, 4);  // GAME FILES, UPDATES, REPORT A PROBLEM, OPEN MY DATA FOLDER, then the game's entry
  host.shot(m, "entry");
  enter(m);
  EXPECT(!m.is_open() && host.said == (std::vector<std::string>{"pause", "resume", "pick"}));
  EXPECT(host.logged("menu: the game's UPGRADES"));
  // Without entries the sections' ring goes on to RETURN TO GAME.
  Host bare;
  GameMenu b(bare.hooks(), {});
  b.show();
  down(b, 4);
  enter(b);
  EXPECT(!b.is_open() && bare.said == (std::vector<std::string>{"pause", "resume"}));
}

// REPORT A PROBLEM: the log's folder opened (a file:// address, escaped), its path copied, the report page opened (a
// catalog's address in place of the game's built-in one; anything not https:// ignored).
TEST(game_menu_report_page_opens_and_copies) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  MenuHooks h = host.hooks();
  h.log_file = tmp("user") + "/my logs/demo.log";
  GameMenu m(std::move(h), {});
  m.show();
  down(m, 2);
  host.shot(m, "report");
  right(m), enter(m);
  EXPECT(host.said.size() == 2 && host.said[1].rfind("open file://", 0) == 0 &&
         host.said[1].find("game-menu/user/my%20logs") != std::string::npos);
  down(m), enter(m);
  EXPECT(host.has("copy " + tmp("user") + "/my logs/demo.log"));
  down(m), enter(m);
  EXPECT(host.has("open https://github.com/example/demo/issues"));
  m.set_report_url("http://example.org/plain");
  enter(m);
  EXPECT(host.said.back() == "open https://github.com/example/demo/issues");
  m.set_report_url("https://example.org/report");
  enter(m);
  EXPECT(host.said.back() == "open https://example.org/report" && m.is_open());
}

// GAME FILES' CHANGE asks first; KEEP PLAYING goes back; CLOSE AND CHANGE closes the menu and the game's hook restarts it.
TEST(game_menu_change_asks_then_restarts) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  GameMenu m(host.hooks(), {});
  m.show();
  host.shot(m, "files");
  right(m), enter(m);  // CHANGE: the question, KEEP PLAYING lit
  host.shot(m, "files-change");
  enter(m);
  EXPECT(m.is_open() && !host.has("restart"));
  enter(m);      // CHANGE again
  up(m), enter(m);  // CLOSE AND CHANGE
  EXPECT(!m.is_open() && host.said == (std::vector<std::string>{"pause", "resume", "restart"}));
  // A game with no restart hook offers no CHANGE.
  Host bare;
  MenuHooks h = bare.hooks();
  h.restart_for_setup = nullptr;
  GameMenu b(std::move(h), {});
  b.show();
  right(b), enter(b);
  EXPECT(b.is_open() && bare.said == std::vector<std::string>{"pause"});
}

// OPEN MY DATA FOLDER and the mouse: a click on a section shows its page; its buttons answer the keys.
TEST(game_menu_data_folder_and_the_mouse) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  GameMenu m(host.hooks(), {});
  m.show();
  SDL_Event click{};
  click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  click.button.button = SDL_BUTTON_LEFT, click.button.x = 260, click.button.y = 70 + 86 + 46 * 3 + 20;  // its row
  EXPECT(m.event(click));
  host.shot(m, "data");
  enter(m);  // OPEN THE FOLDER, lit by the click
  EXPECT(host.said.size() == 2 && host.said[1].rfind("open file://", 0) == 0);
  down(m), enter(m);
  EXPECT(host.has("copy " + tmp("user")));
  tap(m, SDLK_LEFT, SDL_SCANCODE_LEFT);
  up(m), up(m);  // UPDATES: an empty page for now (step 3)
  host.shot(m, "updates");
  EXPECT(m.is_open());
}

// The picture: the scrim over the game, the panel, sent again only when it changes.
TEST(game_menu_draws_over_the_game) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  Host host;
  rekit::sdl::Theme theme;
  GameMenu m(host.hooks(), theme);
  m.show();
  host.frame(m);
  host.frame(m);
  EXPECT(host.draws == 2 && host.uploads == 1);
  // The frame read back before it is shown: the game dimmed outside the panel, the panel's colour inside it.
  host.game_and_menu(m);
  const SDL_Color out = host.pixel(40, 40), in = host.pixel(900, 600);
  EXPECT(out.r < 72 / 2 && out.r > 5 && out.b < 96 / 2);
  EXPECT(in.r == theme.panel.r && in.g == theme.panel.g && in.b == theme.panel.b);
  SDL_RenderPresent(host.ren);
  down(m);
  host.frame(m);
  EXPECT(host.uploads == 2);
}

TEST(catalog_report_reads_the_entrys_page) {
  const std::string cat = R"({"games": [{"key": "demo", "report": "https://example.org/demo/issues"},)"
                          R"({"key": "plain", "report": "http://example.org/x"}, {"key": "none"}]})";
  EXPECT(rekit::catalog_report(cat, "demo") == "https://example.org/demo/issues");
  EXPECT(rekit::catalog_report(cat, "plain").empty() && rekit::catalog_report(cat, "none").empty());
  EXPECT(rekit::catalog_report(cat, "other").empty() && rekit::catalog_report("not json", "demo").empty());
}
