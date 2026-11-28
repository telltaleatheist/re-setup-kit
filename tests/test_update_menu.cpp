// UPDATES in the in-game menu (game_menu.h, sdl/update.h) against the file:/// world of update_world.h: the start-up
// prompt and its three answers, the page (notes, the checkbox, CHECK NOW, the backup line, UPDATE NOW asking to close),
// the badge, UPDATE NOW to the end (an AppImage swapped, the game asked to quit) and a copy that can't update itself.
// REKIT_MENU_SHOTS=<dir> saves each screen (menu-update-*.bmp).
#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "check.h"
#include "rekit/sdl/game_menu.h"
#include "rekit/update.h"
#include "update_world.h"

namespace fs = std::filesystem;
using rekit::sdl::GameMenu;
using rekit::sdl::MenuHooks;
using rekit::sdl::MenuImage;
using update_world::Env;
using update_world::World;

namespace {

const char* const kNotes =
    "- RUMBLE on the Game Options screen, under RAW MOUSE, turns a pad's rumble off.\\n- A gamepad bound to the throttle in "
    "Assign Axis keeps the zero point you set again.\\n- A command bound only to a mouse button works without a key.";
const char* const kOlder =
    ",{\"tag_name\": \"v1.3.0\", \"body\": \"## Fixes\\n- The jump jets' heat is shown again.\", \"html_url\": "
    "\"https://h.example/v1.3.0\", \"assets\": [{\"name\": \"Demo-v1.3.0.AppImage\", \"size\": 15, \"browser_download_url\": "
    "\"file:///nowhere/Demo-v1.3.0.AppImage\"}]}";

struct Host {
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;
  uint64_t serial = 0;
  std::vector<std::string> said, log;
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
  MenuHooks hooks(const World& w, const fs::path& user) {
    MenuHooks h;
    h.window = win;
    h.draw = [this](const MenuImage& im) {
      if (!tex) {
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, im.w, im.h);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      }
      if (im.serial != serial) SDL_UpdateTexture(tex, nullptr, im.pixels, im.pitch), serial = im.serial;
      SDL_RenderTexture(ren, tex, nullptr, nullptr);
    };
    h.pause = [this] { said.push_back("pause"); };
    h.resume = [this] { said.push_back("resume"); };
    h.title = "Demo Game";
    h.version = "1.2.0";
    h.user_folder = user.string();
    h.install = rekit::InstallKind::kAppImage;
    h.update = w.src;
    h.quit = [this] { said.push_back("quit"); };
    h.log = [this](const std::string& l) { log.push_back(l); };
    h.open_url = [this](const std::string& u) { return said.push_back("open " + u), true; };
    return h;
  }
  void frame(GameMenu& m) {
    SDL_SetRenderDrawColor(ren, 72, 82, 96, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 107, 102, 86, 255);
    const SDL_FRect ground{0, 280, 1280, 440};
    SDL_RenderFillRect(ren, &ground);
    m.frame();
  }
  // Frames until `done` (at most 10 s).
  template <class F>
  bool until(GameMenu& m, F done) {
    for (int i = 0; i < 400; ++i) {
      frame(m);
      SDL_RenderPresent(ren);
      if (done()) return true;
      SDL_Delay(25);
    }
    return false;
  }
  void shot(GameMenu& m, const char* name) {
    const char* dir = SDL_getenv("REKIT_MENU_SHOTS");
    if (!dir || !*dir) return;
    frame(m);
    if (SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr)) {
      SDL_SaveBMP(s, (fs::path(dir) / (std::string("menu-update-") + name + ".bmp")).string().c_str());
      SDL_DestroySurface(s);
    }
    SDL_RenderPresent(ren);
  }
  bool has(const std::string& s) const {
    for (const std::string& l : said)
      if (l == s) return true;
    return false;
  }
  bool logged(const std::string& s) const { return times(s) > 0; }
  int times(const std::string& s) const {
    int n = 0;
    for (const std::string& l : log) n += l.find(s) != std::string::npos;
    return n;
  }
};

SDL_Event key(bool down, SDL_Keycode k, SDL_Scancode sc) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.type = static_cast<SDL_EventType>(e.type);
  e.key.key = k, e.key.scancode = sc, e.key.down = down;
  return e;
}
void tap(GameMenu& m, SDL_Keycode k, SDL_Scancode sc) {
  m.event(key(true, k, sc));
  m.event(key(false, k, sc));
}
void enter(GameMenu& m) { tap(m, SDLK_RETURN, SDL_SCANCODE_RETURN); }
// The menu's keys: Cmd + Shift + F1 on the Mac, Shift + Alt + F1 elsewhere.
void menu_keys(GameMenu& m) {
  SDL_Event e = key(true, SDLK_F1, SDL_SCANCODE_F1);
#if defined(__APPLE__)
  e.key.mod = SDL_KMOD_LGUI | SDL_KMOD_LSHIFT;
#else
  e.key.mod = SDL_KMOD_LALT | SDL_KMOD_LSHIFT;
#endif
  m.event(e);
  e = key(false, SDLK_F1, SDL_SCANCODE_F1);
  m.event(e);
}

}  // namespace

// A start not from the launcher with a newer signed release: UPDATE AVAILABLE in the corner, the game neither paused
// nor covered (2026-11-28); the menu's keys then open the prompt, paused. NOT NOW (Esc) closes it and the next day's
// check says it again; DON'T ASK AGAIN unticks the page's checkbox, so no later start says it.
TEST(update_menu_start_prompt) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", nullptr);
  World w("menu-prompt", "Demo-{tag}.AppImage", "the new program", 1, 1, kNotes);
  const fs::path user = w.root / "user";
  int64_t now = 1764000000;
  w.src.now_s = [&now]() { return now; };
  Host host;
  {
    GameMenu m(host.hooks(w, user), {});
    EXPECT(host.until(m, [&] { return host.logged("the update notice: Demo Game 1.4.0"); }));
    EXPECT(!m.is_open() && !host.has("pause") && !host.logged("the update prompt"));
    const uint64_t t0 = SDL_GetTicks();  // past the hint's fade in
    host.until(m, [&] { return SDL_GetTicks() - t0 > 700; });
    host.shot(m, "notice");
    menu_keys(m);
    EXPECT(m.is_open() && host.has("pause") && host.logged("the update prompt: Demo Game 1.4.0"));
    host.shot(m, "prompt");
    tap(m, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    EXPECT(!m.is_open() && host.logged("the update prompt: NOT NOW") && rekit::read_update_prefs(user.string()).ask_at_start);
  }
  {
    GameMenu m(host.hooks(w, user), {});  // the same day: not said
    host.until(m, [] { return false; });
    EXPECT(!m.is_open() && host.times("the update notice") == 1);
    menu_keys(m);  // the menu's own first page, no prompt waiting
    EXPECT(m.is_open() && host.times("the update prompt: Demo") == 1);
    tap(m, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
  }
  now += 86400;
  {
    GameMenu m(host.hooks(w, user), {});
    EXPECT(host.until(m, [&] { return host.times("the update notice") == 2; }) && !m.is_open());
    menu_keys(m);
    EXPECT(m.is_open());
    tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), enter(m);  // DON'T ASK AGAIN
    EXPECT(!m.is_open() && host.logged("DON'T ASK AGAIN") && !rekit::read_update_prefs(user.string()).ask_at_start);
  }
  now += 86400 * 2;
  {
    GameMenu m(host.hooks(w, user), {});
    host.until(m, [] { return false; });
    EXPECT(!m.is_open() && host.times("the update notice") == 2);
  }
}

// The live values: the signed catalog read for the kit's settings on its own only as the start-up check would be (not
// under the launcher, not with the check turned off), and always when the player opens the menu.
TEST(update_menu_live_values_follow_the_check_rules) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", nullptr);
  World w("menu-live", "Demo-{tag}.AppImage", "the new program");
  std::string cat = update_world::read(w.root / "catalog.json");
  cat.replace(cat.find("{\"catalog\": 1,"), 14,
              "{\"catalog\": 1, \"setup_kit\": {\"hint_seconds\": 5}, \"games\": [{\"key\": \"demo\", \"report\": "
              "\"https://example.com/live-report\"}],");
  update_world::put(w.root / "catalog.json", cat), update_world::put(w.root / "catalog.json.sig", test_keys::sign(cat, 1));
  Host host;
  auto start = [&](const char* name, bool launcher, bool checks, bool open) {
    const fs::path user = w.root / name;
    fs::create_directories(user);
    if (!checks) rekit::write_update_prefs(user.string(), {false, 0});
    env.set("RE_LAUNCHER", launcher ? "2.2.0" : nullptr);
    {
      GameMenu m(host.hooks(w, user), {});
      if (open) m.show();
      for (int i = 0; i < 80 && !fs::exists(user / "setup-kit-live.json"); ++i) SDL_Delay(25);  // a file:// world: well under 2 s
    }
    env.set("RE_LAUNCHER", nullptr);
    const std::string live = update_world::read(user / "setup-kit-live.json");
    std::printf("  %s: %s\n", name, live.empty() ? "not read" : live.c_str());
    return live.find("https://example.com/live-report") != std::string::npos && live.find("\"hint_seconds\":5") != std::string::npos;
  };
  const bool alone = start("alone", false, true, false), launcher = start("launcher", true, true, false),
             off = start("off", false, false, false), opened = start("opened", true, false, true);
  EXPECT(alone && !launcher && !off && opened);
}

#if !defined(_WIN32)
// UPDATE NOW from the prompt: the page asks to close the game; CLOSE AND UPDATE downloads, backs up, swaps the AppImage
// and asks the game to quit. The page before it: two versions' notes, the badge, the checkbox, the backup line.
TEST(update_menu_update_now) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", nullptr);
  World w("menu-update", "Demo-{tag}.AppImage", "the new program", 1, 1, kNotes, kOlder);
  const fs::path user = w.root / "user", app = w.root / "apps" / "Demo.AppImage";
  update_world::put(app, "the old program"), update_world::put(user / "saves" / "one.sav", "progress");
  w.src.target = app.string(), w.src.keep = {(user / "saves").string()};
  Host host;
  GameMenu m(host.hooks(w, user), {});
  EXPECT(host.until(m, [&] { return host.logged("the update notice"); }) && !m.is_open());
  m.show();  // GAME FILES, then UPDATES below it
  tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN);
  host.shot(m, "page");
  tap(m, SDLK_RIGHT, SDL_SCANCODE_RIGHT);  // into the page: the checkbox first
  enter(m);
  EXPECT(!rekit::read_update_prefs(user.string()).ask_at_start && host.logged("check at start: off"));
  enter(m);
  EXPECT(rekit::read_update_prefs(user.string()).ask_at_start);
  m.hide();
  // From a prompt (a start a day later would show it; here the page is reached as the prompt's UPDATE NOW leaves it).
  m.show();
  tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), tap(m, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
  tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN);  // CHECK NOW, UPDATE NOW
  enter(m);
  host.shot(m, "confirm");
  EXPECT(m.is_open() && update_world::read(app) == "the old program");
  tap(m, SDLK_UP, SDL_SCANCODE_UP), enter(m);  // KEEP PLAYING is lit; up to CLOSE AND UPDATE
  EXPECT(host.until(m, [&] { return host.has("quit"); }));
  EXPECT(!m.is_open() && update_world::read(app) == "the new program" && fs::exists(user / rekit::kUpdateNoteName));
  EXPECT(fs::exists(user / "Backups") && host.logged("menu: updated:"));
  EXPECT(host.logged("menu: update: downloaded Demo-v1.4.0.AppImage from ") && host.logged("SHA256SUMS signed with the game's key") &&
         host.logged("menu: update: swapped in (") && !host.logged("the swap runs once"));
}
#endif

// An earlier UPDATE NOW that did not get there (the old version starting again, as after a declined Windows prompt): the
// prompt says so once, even started by the launcher with the start-up check unticked, with UPDATE NOW and the release
// page; nothing is counted, and the next start says nothing.
TEST(update_menu_unfinished_notice) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");
  World w("menu-unfinished", "Demo-{tag}.AppImage", "the new program", 1, 1, kNotes);
  const fs::path user = w.root / "user", args = w.root / "curl-args.txt";
  env.set("REKIT_CURL_ARGS_TO", args.string().c_str());
  rekit::write_update_prefs(user.string(), {false, 0});
  rekit::write_update_note(user.string(), {"1.2.0", "1.4.0", "https://counts.example/count", true});
  Host host;
  {
    GameMenu m(host.hooks(w, user), {});
    EXPECT(host.until(m, [&] { return host.logged("the update notice: 1.4.0 did not finish (this is 1.2.0)"); }) && !m.is_open());
    EXPECT(!fs::exists(user / rekit::kUpdateNoteName));
    const uint64_t t0 = SDL_GetTicks();
    host.until(m, [&] { return SDL_GetTicks() - t0 > 700; });
    host.shot(m, "unfinished-notice");
    menu_keys(m);
    EXPECT(m.is_open() && host.logged("the update did not finish: 1.4.0 (this is 1.2.0)"));
    host.shot(m, "unfinished");
    tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), enter(m);  // RELEASE PAGE
    EXPECT(!m.is_open() && host.has("open https://h.example/v1.4.0"));
  }
  EXPECT(update_world::read(args).find("kind=update") == std::string::npos);
  {
    GameMenu m(host.hooks(w, user), {});
    host.until(m, [] { return false; });
    EXPECT(!m.is_open());
  }
}

// CHECK NOW when up to date; a copy that can't update itself (a development build) offers the release page instead.
TEST(update_menu_check_now_and_blocked) {
  Env env;
  env.set("RE_KIT_TEST", nullptr), env.set("RE_LAUNCHER", "2.2.0");  // the launcher's start: no prompt
  World w("menu-check", "Demo-{tag}.AppImage", "the new program", 1, 1, kNotes);
  const fs::path user = w.root / "user";
  w.src.recipe.tag = "v1.4.0";
  Host host;
  {
    GameMenu m(host.hooks(w, user), {});
    host.until(m, [] { return false; });
    EXPECT(!m.is_open());
    m.show();
    tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), tap(m, SDLK_RIGHT, SDL_SCANCODE_RIGHT), tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN);
    enter(m);  // CHECK NOW
    EXPECT(host.until(m, [&] { return host.logged("menu: checked: Up to date (1.4.0)."); }));
    host.shot(m, "up-to-date");
  }
  w.src.recipe.tag = "v1.2.0";
  MenuHooks h = host.hooks(w, user);
  h.install = rekit::InstallKind::kDevelopment;
  GameMenu m(h, {});
  m.show();
  tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), tap(m, SDLK_RIGHT, SDL_SCANCODE_RIGHT), tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN);
  enter(m);
  EXPECT(host.until(m, [&] { return host.logged("menu: checked: Version 1.4.0 is available"); }));
  host.shot(m, "blocked");
  tap(m, SDLK_DOWN, SDL_SCANCODE_DOWN), enter(m);  // OPEN THE RELEASE PAGE
  EXPECT(host.has("open https://h.example/v1.4.0"));
}
