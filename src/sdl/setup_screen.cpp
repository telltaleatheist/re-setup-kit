#include "rekit/sdl/setup_screen.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "rekit/archive.h"
#include "rekit/game_folder.h"
#include "rekit/sdl/pickers.h"
#include "setup_ops.h"
#include "setup_text.h"

namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

// 16:9, the plates and boxes in a 640-wide block in the middle (kX in), as mw3-re's launcher lays them out.
constexpr int W = 854, H = 480, kX = (W - 640) / 2;

enum Id { kFind = 10, kBack = 12, kGo = 30, kCancel, kPickOther = 33, kInstallTo };

struct Button {
  int id = 0;
  SDL_Rect r{};
  std::string label, sub;
  bool enabled = true;
};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
// An operation on a worker thread.
struct Work {
  std::thread th;
  std::mutex m;
  Progress progress;
  InstallResult result;
  std::atomic<bool> cancel{false}, done{false};
  std::string title;
  enum Kind { kLook, kInstall } kind = kLook;
  ~Work() {
    cancel = true;
    if (th.joinable()) th.join();
  }
  void start(std::function<InstallResult(const ProgressFn&)> op) {
    th = std::thread([this, op] {
      InstallResult r = op([this](const Progress& p) {
        std::lock_guard<std::mutex> g(m);
        progress = p;
        return !cancel.load();
      });
      std::lock_guard<std::mutex> g(m);
      result = std::move(r);
      done = true;
    });
  }
};

// A picked file or folder (SDL's dialogs answer through a callback, on another thread on some systems).
struct Pick {
  std::mutex m;
  int what = 0;
  std::string path;
  bool answered = false, cancelled = false;
  static void SDLCALL done(void* user, const char* const* list, int) {
    auto* p = static_cast<Pick*>(user);
    std::lock_guard<std::mutex> g(p->m);
    p->answered = true;
    if (list && list[0]) p->path = list[0];
    else p->cancelled = true;
  }
};

struct Sound {
  SDL_AudioSpec spec{};
  Uint8* buf = nullptr;
  Uint32 len = 0;
  ~Sound() { SDL_free(buf); }
};

class Screen {
 public:
  Screen(const Game& g, const Theme& t, const SetupOptions& o) : g_(g), t_(t), o_(o) {}
  ~Screen();
  SetupResult run();

 private:
  enum Kind { kFindScreen, kConfirm, kWorking } screen_ = kFindScreen;

  void say(const char* fmt, const std::string& a = {}, const std::string& b = {});
  void layout();
  void draw();
  void plate(const Button& b, bool hot);
  void activate(int id);
  void nav(int action);
  void search();
  void look_at(const std::string& path, const std::string& file = {});
  void picked(int what, const std::string& path);
  void ask_file(int what);
  void ask_folder(int what);
  void open_picker();
  void finish_work();
  void play(const Sound& s);
  std::string out_dir() const;
  std::string what_to_pick() const { return detail::what_to_pick(g_); }
  std::string find_words() const { return detail::find_words(g_); }
  std::string found_words(const Found& f) const { return detail::found_words(g_, f); }
  void confirm_text();
  void load_art();
  void blit_png(SDL_Texture* t, const SDL_FRect& d);

  const Game& g_;
  const Theme& t_;
  SetupOptions o_;
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture *badge_ = nullptr, *background_ = nullptr, *band_ = nullptr, *plate_ = nullptr;
  int plate_w_ = 0, plate_h_ = 0;
  detail::Text text_;
  std::vector<Button> buttons_;
  int hot_ = -1, focus_ = -1;
  std::string status_;
  bool status_good_ = true;
  std::unique_ptr<Work> work_;
  std::shared_ptr<Found> found_;
  std::string found_name_, found_sub_, words_, into_, confirm_text_, confirm_into_;
  Pick pick_;
  bool asking_ = false, ask_pending_ = false, ask_folder_ = false;
  Uint64 ask_at_ = 0;
  bool auto_go_ = false, pick_other_ = false;
  bool done_ = false;
  SetupResult result_;
  Sound click_, hover_;
  SDL_AudioStream* audio_ = nullptr;
  bool audio_tried_ = false;
};

Screen::~Screen() {
  work_.reset();
  for (SDL_Texture* t : {badge_, background_, band_, plate_})
    if (t) SDL_DestroyTexture(t);
  if (audio_) SDL_DestroyAudioStream(audio_);
  if (ren_) SDL_DestroyRenderer(ren_);
  if (win_) SDL_DestroyWindow(win_);
}

void Screen::say(const char* fmt, const std::string& a, const std::string& b) {
  // fmt is one of this file's literals, with one or two %s.
  const int n = std::snprintf(nullptr, 0, fmt, a.c_str(), b.c_str());
  std::string line(n > 0 ? static_cast<size_t>(n) : 0, '\0');
  if (n > 0) std::snprintf(line.data(), line.size() + 1, fmt, a.c_str(), b.c_str());
  if (o_.log) return o_.log(line);
  std::printf("%s%s\n", o_.log_prefix.c_str(), line.c_str());
  std::fflush(stdout);
}

std::string Screen::out_dir() const {
  return detail::out_dir(into_, o_.default_out);
}

void Screen::confirm_text() {
  confirm_text_ = words_ + " Your own files are only read, and may be deleted afterwards.";
  confirm_into_ = out_dir();
  say("game files into %s", confirm_into_);
}

void Screen::layout() {
  buttons_.clear();
  auto add = [&](int id, SDL_Rect r, std::string label, std::string sub = {}, bool on = true) {
    r.x += kX;
    buttons_.push_back({id, r, std::move(label), std::move(sub), on});
  };
  switch (screen_) {
    case kFindScreen: {
      add(kFind, {130, 222, 380, 52}, "FIND THE GAME", detail::find_sub(g_));
      add(kBack, {250, 384, 140, 34}, o_.play_button && !o_.in_use.empty() ? "PLAY" : "QUIT");
      break;
    }
    case kConfirm:
      add(kPickOther, {130, 180, 380, 56}, found_name_, found_sub_);
      add(kGo, {85, 330, 140, 40}, "INSTALL");
      add(kInstallTo, {240, 330, 160, 40}, "CHANGE FOLDER");
      add(kBack, {415, 330, 140, 40}, "BACK");
      break;
    case kWorking: add(kCancel, {250, 372, 140, 34}, "CANCEL"); break;
  }
  static const char* const kNames[] = {"find", "confirm", "work"};
  std::string labels;
  for (const Button& b : buttons_) labels += (labels.empty() ? "" : ", ") + b.label;
  say("screen %s: %s", kNames[screen_], labels);
  say("header %s", t_.header.empty() ? upper(g_.title) : t_.header);
  say("status %s", status_.empty() ? "(none)" : status_);
  if (screen_ == kFindScreen && !o_.notice.empty()) say("notice %s", o_.notice);
  hot_ = focus_ = -1;
}

void Screen::blit_png(SDL_Texture* t, const SDL_FRect& d) { SDL_RenderTexture(ren_, t, nullptr, &d); }

void Screen::plate(const Button& b, bool hot) {
  const SDL_FRect r{static_cast<float>(b.r.x), static_cast<float>(b.r.y), static_cast<float>(b.r.w), static_cast<float>(b.r.h)};
  if (plate_) {
    const Uint8 m = !b.enabled ? 90 : hot ? 255 : 215;
    SDL_SetTextureColorMod(plate_, m, m, m);
    if (t_.plate_slice > 0) {
      const float s = static_cast<float>(t_.plate_slice);
      SDL_RenderTexture9Grid(ren_, plate_, nullptr, s, s, s, s, 1.0f, &r);
    } else {
      SDL_RenderTexture(ren_, plate_, nullptr, &r);
    }
  } else {
    const SDL_Color c = b.enabled ? t_.plate_colour : t_.plate_off;
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(ren_, &r);
  }
  if (hot) {  // a square plate lights its edge in the accent
    SDL_SetRenderDrawColor(ren_, t_.accent.r, t_.accent.g, t_.accent.b, 255);
    SDL_RenderRect(ren_, &r);
    const SDL_FRect in{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    SDL_SetRenderDrawColor(ren_, static_cast<Uint8>(t_.accent.r * 3 / 5), static_cast<Uint8>(t_.accent.g * 3 / 5),
                           static_cast<Uint8>(t_.accent.b * 3 / 5), 255);
    SDL_RenderRect(ren_, &in);
  }
  const SDL_Color grey{110, 110, 116, 255};
  const SDL_Color c = !b.enabled ? grey : hot ? t_.accent : t_.text;
  const int cx = b.r.x + b.r.w / 2, cy = b.r.y + b.r.h / 2;
  int big = b.r.h >= 50 ? 22 : b.r.h >= 40 ? 20 : 18;
  while (big > 12 && text_.width(b.label, big) > b.r.w - 14) --big;  // a wide font's label fits its plate
  if (b.sub.empty()) {
    text_.draw(ren_, b.label, big, c, cx, cy + 1);
  } else {
    text_.draw(ren_, text_.fit(b.label, big, b.r.w - 20), big, c, cx, cy - 11);
    text_.draw(ren_, text_.fit(b.sub, 12, b.r.w - 20), 12, b.enabled ? t_.dim : grey, cx, cy + 8);
  }
}

void Screen::draw() {
  int pw = 0, ph = 0;
  SDL_GetCurrentRenderOutputSize(ren_, &pw, &ph);
  text_.set_scale(std::max(1.0f, std::min(static_cast<float>(pw) / W, static_cast<float>(ph) / H)));
  SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
  SDL_RenderClear(ren_);
  SDL_SetRenderDrawColor(ren_, t_.bg.r, t_.bg.g, t_.bg.b, 255);
  const SDL_FRect all{0, 0, W, H};
  SDL_RenderFillRect(ren_, &all);
  if (background_) {  // scaled to cover
    float tw = 0, th = 0;
    SDL_GetTextureSize(background_, &tw, &th);
    const float k = std::max(W / tw, H / th);
    blit_png(background_, {(W - tw * k) / 2, (H - th * k) / 2, tw * k, th * k});
  }
  // The header and footer bands, a rule under the header and over the footer.
  const SDL_FRect top{0, 0, W, 96}, bottom{0, H - 48, W, 48};
  if (band_) {
    blit_png(band_, top), blit_png(band_, bottom);
  } else {
    SDL_SetRenderDrawColor(ren_, t_.band_colour.r, t_.band_colour.g, t_.band_colour.b, 255);
    SDL_RenderFillRect(ren_, &top), SDL_RenderFillRect(ren_, &bottom);
  }
  SDL_SetRenderDrawColor(ren_, t_.rule.r, t_.rule.g, t_.rule.b, 255);
  const SDL_FRect l1{0, 96, W, 1}, l2{0, H - 49, W, 1};
  SDL_RenderFillRect(ren_, &l1), SDL_RenderFillRect(ren_, &l2);
  {  // the game it sets up: its badge and its title
    const std::string name = t_.header.empty() ? upper(g_.title) : t_.header;
    const int size = text_.width(name, 36) > 520 ? 30 : 36;
    const int badge = badge_ ? 64 : 0, gap = badge_ ? 16 : 0, w = badge + gap + text_.width(name, size), x = (W - w) / 2;
    if (badge_) blit_png(badge_, {static_cast<float>(x), 48.0f - badge / 2, static_cast<float>(badge), static_cast<float>(badge)});
    text_.draw(ren_, name, size, t_.text, x + badge + gap, 48, -1);
  }
  const SDL_Color box_colour{16, 18, 22, 255};
  switch (screen_) {
    case kFindScreen: {
      const SDL_FRect box{kX + 70.0f, 104, 500, 64};
      SDL_SetRenderDrawColor(ren_, box_colour.r, box_colour.g, box_colour.b, 255);
      SDL_RenderFillRect(ren_, &box);
      text_.draw_wrapped(ren_, find_words(), 13, t_.text, W / 2, 114, 480);
      if (!status_.empty()) text_.draw_wrapped(ren_, status_, 13, status_good_ ? t_.good : t_.bad, W / 2, 184, 480);
      if (!o_.notice.empty()) text_.draw_wrapped(ren_, o_.notice, 13, t_.bad, W / 2, 290, 480);
      break;
    }
    case kConfirm: {
      text_.draw(ren_, "USE THESE GAME FILES?", 18, t_.accent, W / 2, 150);
      const int h = text_.draw_wrapped(ren_, confirm_text_, 14, t_.text, W / 2, 252, 460);
      text_.draw(ren_, "Into " + text_.fit(confirm_into_, 14, 440, 3), 14, t_.accent, W / 2, std::min(252 + h + 12, 314));
      if (!status_.empty()) text_.draw_wrapped(ren_, status_, 13, status_good_ ? t_.good : t_.bad, W / 2, 392, 480);
      break;
    }
    case kWorking: {
      Progress p;
      std::string title;
      if (work_) {
        std::lock_guard<std::mutex> g(work_->m);
        p = work_->progress, title = work_->title;
      }
      text_.draw(ren_, title, 26, t_.accent, W / 2, 140);
      const float f = static_cast<float>(std::clamp(p.fraction, 0.0, 1.0));
      const SDL_FRect bar{kX + 120.0f, 200, 400, 26}, inner{kX + 122.0f, 202, 396, 22}, in{kX + 123.0f, 203, 394 * f, 20};
      SDL_SetRenderDrawColor(ren_, static_cast<Uint8>(t_.accent.r * 5 / 9), static_cast<Uint8>(t_.accent.g * 5 / 9),
                             static_cast<Uint8>(t_.accent.b * 5 / 9), 255);
      SDL_RenderFillRect(ren_, &bar);
      SDL_SetRenderDrawColor(ren_, 10, 12, 14, 255);
      SDL_RenderFillRect(ren_, &inner);
      SDL_SetRenderDrawColor(ren_, static_cast<Uint8>(t_.accent.r * 5 / 6), static_cast<Uint8>(t_.accent.g * 5 / 6),
                             static_cast<Uint8>(t_.accent.b * 5 / 6), 255);
      SDL_RenderFillRect(ren_, &in);
      char pct[16];
      std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(p.fraction * 100)));
      text_.draw(ren_, pct, 14, t_.text, W / 2, 214);
      text_.draw(ren_, text_.fit(p.stage, 13, 600), 13, t_.text, W / 2, 246);
      text_.draw(ren_, text_.fit(p.message, 10, 600), 10, t_.dim, W / 2, 266);
      text_.draw(ren_, "Cancel stops within a file and changes nothing.", 12, t_.dim, 16, H - 24, -1);
      break;
    }
  }
  const int hot = focus_ >= 0 ? focus_ : hot_;
  for (size_t i = 0; i < buttons_.size(); ++i) plate(buttons_[i], static_cast<int>(i) == hot && buttons_[i].enabled);
  if (ask_pending_) {  // the frame left on screen while the system's picker comes up
    const SDL_FRect edge{W / 2 - 151.0f, H - 42.0f, 302, 32}, box{W / 2 - 150.0f, H - 41.0f, 300, 30};
    SDL_SetRenderDrawColor(ren_, 140, 100, 50, 255);
    SDL_RenderFillRect(ren_, &edge);
    SDL_SetRenderDrawColor(ren_, 10, 12, 14, 255);
    SDL_RenderFillRect(ren_, &box);
    text_.draw(ren_, ask_folder_ ? "Opening the folder picker..." : "Opening the file picker...", 13, t_.accent, W / 2, H - 26);
  }
}

void Screen::play(const Sound& s) {
  if (!s.buf || o_.hidden) return;
  if (!audio_ && !audio_tried_) {
    audio_tried_ = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
      audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &s.spec, nullptr, nullptr);
    if (audio_) SDL_ResumeAudioStreamDevice(audio_);
  }
  if (!audio_) return;
  SDL_SetAudioStreamFormat(audio_, &s.spec, nullptr);
  SDL_ClearAudioStream(audio_);
  SDL_PutAudioStreamData(audio_, s.buf, static_cast<int>(s.len));
}

void Screen::ask_file(int what) {
  {
    std::lock_guard<std::mutex> g(pick_.m);
    pick_.what = what, pick_.path.clear(), pick_.answered = pick_.cancelled = false;
  }
  asking_ = ask_pending_ = true, ask_folder_ = false;
}
void Screen::ask_folder(int what) {
  ask_file(what);
  ask_folder_ = true;
}

void Screen::open_picker() {
  ask_pending_ = false, ask_at_ = SDL_GetTicksNS();
  say("the %s picker asked for", ask_folder_ ? "folder" : "file");
  static const SDL_DialogFileFilter kFiles[] = {{"Your copy: a download, an image or any file of the game", "*"}};
  const char* check = SDL_getenv("REKIT_SETUP_PICKER");
  if (check && std::strcmp(check, "cancel") == 0) {
    Pick::done(&pick_, nullptr, 0);
  } else if (check && std::strncmp(check, "path:", 5) == 0) {  // checks: the picker's answer as given
    const char* list[] = {check + 5, nullptr};
    Pick::done(&pick_, list, 0);
  } else {
    PickerAsk a;
    a.kind = ask_folder_ ? PickerKind::kOpenFolder : PickerKind::kOpenFile;
    a.done = Pick::done, a.user = &pick_, a.parent = win_;
    if (!ask_folder_) a.filters = kFiles, a.filter_count = 1;
    if (!::rekit::sdl::open_picker(a)) say("no picker in a run no player is at: cancelled");
  }
}

void Screen::search() {
  const std::string found = detail::search_copy(g_, o_.program_dir);
  if (!found.empty()) return look_at(found);  // offered, to copy in or pass over
  status_ = "Not found on this computer. Pick it: " + what_to_pick() + ".", status_good_ = true;
  screen_ = kFindScreen, layout();
  ask_file(kFind);
}

// `file`: the picked file itself when `path` is the folder standing for it (only with the game's find hook).
void Screen::look_at(const std::string& path, const std::string& file) {
  found_ = std::make_shared<Found>();
  work_ = std::make_unique<Work>();
  work_->kind = Work::kLook;
  work_->title = "LOOKING AT YOUR GAME";
  say("work: %s", work_->title);
  auto found = found_;
  const Game* game = &g_;  // the caller's, kept for the whole of run_setup
  work_->start([path, file, found, game](const ProgressFn& f) { return detail::look(*game, path, file, found.get(), f); });
  screen_ = kWorking, layout();
}

void Screen::picked(int what, const std::string& path) {
  if (what == kInstallTo) {  // the game's own folder (game_folder_in)
    into_ = fs::path(game_folder_in(path, g_.folder_key, g_.folder_name)).make_preferred().string();
    say("%s goes into %s", g_.title, into_);
    confirm_text();
    status_.clear();
    return layout();
  }
  std::string source, file;
  detail::pick_source(g_, path, &source, &file);
  look_at(source, file);
}

void Screen::finish_work() {
  InstallResult r;
  {
    std::lock_guard<std::mutex> g(work_->m);
    r = work_->result;
  }
  if (work_->th.joinable()) work_->th.join();
  const Work::Kind kind = work_->kind;
  work_.reset();
  if (!r.summary.empty()) say("%s", r.summary);
  if (kind == Work::kInstall) {
    if (r.ok) {
      result_.exit = kSetupReady, result_.install_dir = r.install_dir.empty() ? out_dir() : r.install_dir;
      done_ = true;
      return;
    }
    status_ = r.summary, status_good_ = r.cancelled;
    screen_ = kFindScreen, layout();
    return;
  }
  const Found& f = *found_;
  if (!r.ok) {
    status_ = r.summary, status_good_ = r.cancelled;
    screen_ = kFindScreen, layout();
    return;
  }
  std::error_code same;
  if (!o_.same_words.empty() && !o_.in_use.empty() && f.on_disk && fs::equivalent(f.path, o_.in_use, same)) {
    status_ = o_.same_words, status_good_ = true;
    screen_ = kFindScreen, layout();
    return;
  }
  if (f.kind == Found::kInstall && f.on_disk && !o_.default_out.empty() && fs::equivalent(f.path, o_.default_out, same)) {
    // The game's own copy, set up already: used where it is.
    result_.exit = kSetupReady, result_.install_dir = f.path;
    done_ = true;
    return;
  }
  words_ = found_words(f);
  found_name_ = fs::path(f.path).filename().string();
  if (found_name_.empty()) found_name_ = f.path;  // a drive's top
  found_sub_ = "Click to pick other files";
  say("found plate: %s (%s)", found_name_, words_);
  confirm_text();
  status_.clear();  // the find screen's ask ("Not found ... Pick it") is answered
  screen_ = kConfirm;
  if (const char* to = SDL_getenv("REKIT_SETUP_FILES_TO"); to && *to) picked(kInstallTo, to);
  if (pick_other_) {
    pick_other_ = false;
    layout();
    return activate(kPickOther);
  }
  if (auto_go_) {
    auto_go_ = false;
    layout();
    return activate(kGo);
  }
  layout();
}

void Screen::activate(int id) {
  play(click_);
  switch (id) {
    case kFind: search(); break;
    case kPickOther:
      status_ = "Pick the copy to use: " + what_to_pick() + ".", status_good_ = true;
      screen_ = kFindScreen, layout();
      ask_file(kFind);
      break;
    case kInstallTo:
      status_ = "Pick where the game goes. A folder that holds other files gets a folder of the game's own inside it.";
      status_good_ = true;
      ask_folder(kInstallTo);
      break;
    case kGo: {
      if (!into_.empty()) {  // the game's own folder, made the game's before anything goes in
        std::string error;
        if (!claim_game_folder(into_, g_.folder_key, &error)) {
          status_ = "Nothing was copied: " + error + ".", status_good_ = false;
          say("%s", status_);
          layout();
          break;
        }
      }
      if (!g_.install) {
        status_ = "Nothing was copied: this game cannot install from here.", status_good_ = false;
        say("%s", status_);
        layout();
        break;
      }
      work_ = std::make_unique<Work>();
      work_->kind = Work::kInstall;
      work_->title = "INSTALLING " + upper(g_.title);
      say("work: %s", work_->title);
      const auto found = found_;
      const std::string out = out_dir();
      const std::string media = g_.media ? g_.media(out, o_.user_folder) : media_dir(out, o_.user_folder);
      auto install = g_.install;
      work_->start([found, out, media, install](const ProgressFn& f) { return install(*found, out, media, f); });
      screen_ = kWorking, layout();
      break;
    }
    case kCancel:
      if (work_) work_->cancel = true;
      break;
    case kBack:
      if (screen_ == kConfirm) {
        into_.clear(), status_.clear(), screen_ = kFindScreen, layout();
      } else if (screen_ == kFindScreen) {
        if (o_.play_button && !o_.in_use.empty()) result_.exit = kSetupReady, result_.install_dir = o_.in_use;
        done_ = true;
      }
      break;
    default: break;
  }
}

void Screen::nav(int action) {
  const int n = static_cast<int>(buttons_.size());
  const int at = focus_ >= 0 ? focus_ : hot_;
  if (n == 0) return;
  if (action == 1 || action == -1) {
    int f = at;
    for (int i = 0; i < n; ++i) {
      f = ((f < 0 ? (action > 0 ? -1 : 0) : f) + action + n) % n;
      if (buttons_[f].enabled) break;
    }
    focus_ = f;
    play(hover_);
    say("focus %s", buttons_[f].label);
  } else if (action == 2 && at >= 0 && at < n && buttons_[at].enabled) {
    activate(buttons_[at].id);
  } else if (action == 3) {
    activate(screen_ == kWorking ? kCancel : kBack);
  }
}

void Screen::load_art() {
  auto tex = [&](const std::string& path, int* w = nullptr, int* h = nullptr) -> SDL_Texture* {
    if (path.empty()) return nullptr;
    SDL_Surface* s = detail::load_png(path);
    if (!s) {
      say("theme: %s could not be read", path);
      return nullptr;
    }
    if (w) *w = s->w;
    if (h) *h = s->h;
    SDL_Texture* t = SDL_CreateTextureFromSurface(ren_, s);
    SDL_DestroySurface(s);
    if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND), SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
  };
  badge_ = tex(t_.badge), background_ = tex(t_.background), band_ = tex(t_.band), plate_ = tex(t_.plate, &plate_w_, &plate_h_);
  auto wav = [&](const std::string& path, Sound* s) {
    if (!path.empty() && !SDL_LoadWAV(path.c_str(), &s->spec, &s->buf, &s->len)) say("theme: %s could not be read", path);
  };
  wav(t_.click_wav, &click_), wav(t_.hover_wav, &hover_);
}

SetupResult Screen::run() {  // SDL's video is started (run_setup)
  const std::string font = !t_.font.empty() ? t_.font : detail::system_font();
  say("video %s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)");
  if (!text_.open(font)) say("font %s: none could be read, its text will be blank", font.empty() ? "(none found)" : font);
  else say("font %s", font);
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (o_.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  if (o_.hidden) flags |= SDL_WINDOW_HIDDEN;
  win_ = SDL_CreateWindow(g_.title.c_str(), o_.width, o_.height, flags);
  // Hidden (a check): SDL's software renderer, the same pixels everywhere and no graphics driver loaded.
  if (!win_ || !(ren_ = SDL_CreateRenderer(win_, o_.hidden ? SDL_SOFTWARE_RENDERER : nullptr))) {
    result_.exit = kSetupFailed, result_.error = std::string("no window: ") + SDL_GetError();
    return result_;
  }
  SDL_SetRenderLogicalPresentation(ren_, W, H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  if (!o_.hidden) SDL_SetRenderVSync(ren_, 1);
  load_art();
  const bool pads = !o_.hidden && SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  if (!o_.in_use.empty())
    status_ = "In use: " + o_.in_use + (o_.play_button ? ". FIND THE GAME to choose other files, or PLAY to go on." : "."),
    status_good_ = true;
  layout();
  auto_go_ = SDL_getenv("REKIT_SETUP_GO") != nullptr;
  pick_other_ = SDL_getenv("REKIT_SETUP_PICK_OTHER") != nullptr;
  const char* frames_env = SDL_getenv("REKIT_SETUP_FRAMES");
  const long frames_max = frames_env ? std::atol(frames_env) : 0;
  const char* shot = SDL_getenv("REKIT_SETUP_SHOT");
  if (!o_.look_first.empty()) look_at(o_.look_first);
  else if (SDL_getenv("REKIT_SETUP_FIND")) activate(kFind);
  if (const char* steps = SDL_getenv("REKIT_SETUP_NAV")) {
    std::string all = steps;
    for (size_t at = 0; at <= all.size() && !done_;) {
      const size_t comma = std::min(all.find(',', at), all.size());
      const std::string s = all.substr(at, comma - at);
      nav(s == "next" ? 1 : s == "prev" ? -1 : s == "press" ? 2 : s == "back" ? 3 : 0);
      at = comma + 1;
    }
  }
  long frames = 0;
  while (!done_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      SDL_ConvertEventToRenderCoordinates(ren_, &e);
      if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        if (work_) work_->cancel = true;
        done_ = true;
      }
      if (asking_) continue;
      if (screen_ == kWorking && e.type != SDL_EVENT_MOUSE_BUTTON_UP && e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_MOUSE_MOTION) continue;
      if (e.type == SDL_EVENT_MOUSE_MOTION) {
        int h = -1;
        for (size_t i = 0; i < buttons_.size(); ++i) {
          const SDL_Rect& r = buttons_[i].r;
          if (e.motion.x >= r.x && e.motion.x < r.x + r.w && e.motion.y >= r.y && e.motion.y < r.y + r.h) h = static_cast<int>(i);
        }
        if (h != hot_) {
          hot_ = h, focus_ = -1;
          if (h >= 0 && buttons_[h].enabled) play(hover_);
        }
      } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        if (hot_ >= 0 && hot_ < static_cast<int>(buttons_.size()) && buttons_[hot_].enabled) activate(buttons_[hot_].id);
      } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_OpenGamepad(e.gdevice.which);
      } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        int a = 0;
        if (e.type == SDL_EVENT_KEY_DOWN) {
          const SDL_Keycode k = e.key.key;
          if (k == SDLK_DOWN || k == SDLK_RIGHT || k == SDLK_TAB) a = 1;
          else if (k == SDLK_UP || k == SDLK_LEFT) a = -1;
          else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) a = 2;
          else if (k == SDLK_ESCAPE) a = 3;
        } else {
          const int b = e.gbutton.button;
          if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) a = 1;
          else if (b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) a = -1;
          else if (b == SDL_GAMEPAD_BUTTON_SOUTH || b == SDL_GAMEPAD_BUTTON_START) a = 2;
          else if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK) a = 3;
        }
        if (screen_ != kWorking || a == 3 || a == 2) nav(a);
      }
    }
    if (done_) break;
    if (asking_ && !ask_pending_) {
      int what = 0;
      std::string path;
      bool answered = false, cancelled = false;
      {
        std::lock_guard<std::mutex> g(pick_.m);
        answered = pick_.answered, cancelled = pick_.cancelled, what = pick_.what, path = pick_.path;
      }
      if (answered) {
        asking_ = false;
        say("the picker answered (%s) %s ms after it was asked for", cancelled ? "cancelled" : "a pick",
            std::to_string(static_cast<long>((SDL_GetTicksNS() - ask_at_) / 1000000)));
        if (!cancelled) picked(what, path);
        else if (what == kInstallTo) status_.clear(), layout();
      }
    }
    if (work_ && work_->done) finish_work();
    if (done_) break;
    draw();
    const bool last = frames_max <= 0 || (!work_ && frames + 1 >= frames_max);
    if (shot && *shot && last) {  // the frame as drawn, then the end (a review's picture; never committed)
      if (SDL_Surface* s = SDL_RenderReadPixels(ren_, nullptr)) {
        SDL_SaveBMP(s, shot);
        SDL_DestroySurface(s);
        say("shot %s", shot);
      }
      done_ = true;
    }
    SDL_RenderPresent(ren_);
    if (asking_ && ask_pending_) open_picker();  // once the frame with its notice is on screen
    // A fixed count of the frames drawn with nothing at work, so a check's count ends at the same place however
    // long a look or an install takes.
    if (!work_) ++frames;
    else if (o_.hidden) SDL_Delay(1);
    if (frames_max > 0 && frames >= frames_max) {
      say("frames %s: ended", std::to_string(frames));
      if (work_) work_->cancel = true;
      done_ = true;
    }
    if (!o_.hidden && !work_ && !asking_) SDL_WaitEventTimeout(nullptr, 16);
  }
  work_.reset();
  if (pads) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  return result_;
}

}  // namespace

SetupResult run_setup(const Game& game, const Theme& theme, const SetupOptions& options) {
  SetupResult r;
  // Hidden, with video not started and no driver asked for: SDL's dummy driver (a window and the software renderer
  // with no display: ssh, CI; offscreen needs GL on a Mac). A default-priority hint, so SDL_VIDEO_DRIVER still wins,
  // and reset afterwards, so the game's own start is untouched.
  const bool dummy = options.hidden && !SDL_WasInit(SDL_INIT_VIDEO) && !SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
  if (dummy) SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_DEFAULT);
  const bool up = SDL_InitSubSystem(SDL_INIT_VIDEO);
  if (dummy) SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
  if (!up) {
    r.exit = kSetupFailed, r.error = std::string("SDL could not start: ") + SDL_GetError();
    return r;
  }
  {
    Screen s(game, theme, options);  // its window and renderer gone before video stops
    r = s.run();
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  return r;
}

}  // namespace rekit::sdl
