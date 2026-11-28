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
#include "rekit/sdl/setup_original.h"
#include "setup_ops.h"
#include "setup_text.h"

namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

// The setup window (16:9, as the launcher's) and the small one a fresh setup works in (docs/plan-in-game-menu.md
// section 1, mockup screen 5). Logical sizes: each scales to its window, letterboxed.
constexpr int W = 960, H = 540, kSmallW = 600, kSmallH = 270;
// The setup window: the header band, the list of sections at the left, the section's content at the right.
constexpr int kHeadH = 72, kNavW = 180, kCx = 204, kCw = W - 24 - kCx;

enum Id { kFind = 10, kDownload, kBack, kUseFound, kPickFile, kPickFolder, kSearch, kOwnCopy, kGo = 30, kCancel, kPickOther = 33, kInstallTo };
enum Style { kPlain, kPrimary, kLink };

struct Button {
  int id = 0;
  SDL_Rect r{};
  std::string label, sub;
  bool enabled = true;
  Style style = kPlain;
};

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
SDL_Color mix(SDL_Color a, SDL_Color b, int b_parts, int of) {
  auto m = [&](Uint8 x, Uint8 y) { return static_cast<Uint8>((x * (of - b_parts) + y * b_parts) / of); };
  return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
}

// An operation on a worker thread.
struct Work {
  std::thread th;
  std::mutex m;
  Progress progress;
  InstallResult result;
  std::atomic<bool> cancel{false}, done{false};
  std::string title, sub;  // what it is, and what it works on (a download's source and size, a copy's folder)
  enum Kind { kLook, kInstall, kDownload } kind = kLook;
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
  float gain = 1.0f;
  ~Sound() { SDL_free(buf); }
};

class Screen {
 public:
  Screen(const Game& g, const Theme& t, const SetupOptions& o) : g_(g), t_(t), o_(o) {}
  ~Screen();
  SetupResult run();

 private:
  // kFiles: GAME FILES (the setup window's section); kAsking: the small window while the catalog is asked.
  enum Kind { kFiles, kConfirm, kWorking, kAsking } screen_ = kFiles;

  void say(const char* fmt, const std::string& a = {}, const std::string& b = {});
  void layout();
  void draw();
  void draw_head(int w, int h, int badge, int size);
  void draw_files();
  void draw_progress(int x, int y, int w, bool small);
  void panel(int y, int h, const char* label);
  void plate(const Button& b, bool hot);
  void activate(int id);
  void nav(int action);
  void search();
  void search_here();
  void look_at(const std::string& path, const std::string& file = {});
  void picked(int what, const std::string& path);
  void ask_file(int what);
  void ask_folder(int what);
  void open_picker();
  void finish_work();
  void ask_catalog();
  void offer_download();
  void drop_download();
  void start_download();
  void start_work(std::unique_ptr<Work> w, std::function<InstallResult(const ProgressFn&)> op);
  void to_small();
  void to_full(const std::string& status, bool good);
  void size_window();
  void preview(const std::string& what);
  std::string download_sub() const;
  void play(const Sound& s);
  std::string out_dir() const;
  std::string what_to_pick() const { return detail::what_to_pick(g_); }
  std::string find_words() const { return detail::find_words(g_); }
  std::string found_words(const Found& f) const { return detail::found_words(g_, f); }
  void confirm_text();
  void load_look();
  void blit_png(SDL_Texture* t, const SDL_FRect& d);
  int lw() const { return small_ ? kSmallW : W; }
  int lh() const { return small_ ? kSmallH : H; }

  const Game& g_;
  const Theme& t_;
  SetupOptions o_;
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture *badge_ = nullptr, *background_ = nullptr, *band_ = nullptr, *plate_ = nullptr;
  int plate_w_ = 0, plate_h_ = 0;
  detail::Text title_, body_;  // the theme's bold font (titles, buttons, labels) and its body font (sentences)
  std::vector<Button> buttons_;
  int hot_ = -1, focus_ = -1;
  std::string status_;
  bool status_good_ = true;
  // The small window (a fresh setup's automatic first start, 2026-11-27) or the setup window.
  bool small_ = false;
  // FOUND ON THIS COMPUTER: the search's copy ("" none), and whether it has looked; `use_found_`: the find it looks at
  // now is installed without the confirm (the first start's, USE THIS).
  std::string found_here_;
  bool searched_ = false, use_found_ = false, looking_found_ = false;
  // USE MY OWN COPY INSTEAD pressed in the small window: what it works on stops, and the setup window opens.
  bool own_copy_ = false;
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
  std::string nav_install_;  // REKIT_SETUP_NAV_INSTALL, pressed once when the first install or download starts
  std::string preview_;      // REKIT_SETUP_PREVIEW: a review's picture, held
  // DOWNLOAD THEM (Game::original_catalogs): the catalog asked on its own thread at the start.
  std::thread catalog_th_;
  std::mutex catalog_m_;
  std::atomic<bool> catalog_done_{false}, stop_{false};
  bool catalog_wanted_ = false, catalog_asked_ = false, auto_download_ = false, allow_file_ = false;
  bool download_at_once_ = false;  // the first start with none found: the offer downloads as it comes
  std::vector<OriginalSource> catalog_answer_, offer_;  // the thread's answer; the sources offered
  std::string catalog_why_, original_dir_, downloaded_;  // downloaded_: the fetched file, deleted after its look/install
};

Screen::~Screen() {
  stop_ = true;
  work_.reset();
  if (catalog_th_.joinable()) catalog_th_.join();
  drop_download();
  tidy_original_dir(original_dir_);
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
  auto add = [&](int id, SDL_Rect r, std::string label, Style style = kPlain, std::string sub = {}, bool on = true) {
    buttons_.push_back({id, r, std::move(label), std::move(sub), on, style});
  };
  const char* name = "files";
  if (small_) {  // the small window: the link at the left, CANCEL at the right
    name = screen_ == kAsking ? "asking" : "work";
    add(kOwnCopy, {24, 212, 250, 36}, "USE MY OWN COPY INSTEAD", kLink);
    add(kCancel, {kSmallW - 24 - 120, 212, 120, 36}, "CANCEL");
  } else {
    switch (screen_) {
      case kFiles:
      case kAsking:
        screen_ = kFiles;
        if (!found_here_.empty()) add(kUseFound, {kCx + kCw - 16 - 140, 196, 140, 40}, "USE THIS", kPrimary);
        add(kPickFile, {kCx + 16, 332, 160, 36}, "PICK A FILE");
        add(kPickFolder, {kCx + 188, 332, 160, 36}, "PICK A FOLDER");
        add(kSearch, {kCx + 360, 332, 160, 36}, "SEARCH AGAIN");
        // The accent for the one suggested action: USE THIS when a copy was found, else DOWNLOAD.
        if (!offer_.empty()) add(kDownload, {kCx + kCw - 16 - 160, 414, 160, 40}, "DOWNLOAD", found_here_.empty() ? kPrimary : kPlain);
        break;
      case kConfirm:
        name = "confirm";
        add(kPickOther, {kCx, 136, 520, 56}, found_name_, kPlain, found_sub_);
        add(kGo, {kCx, 330, 150, 40}, "INSTALL", kPrimary);
        add(kInstallTo, {kCx + 162, 330, 180, 40}, "CHANGE FOLDER");
        add(kBack, {kCx + 354, 330, 130, 40}, "BACK");
        break;
      case kWorking:
        name = "work";
        add(kCancel, {kCx, 330, 140, 38}, "CANCEL");
        break;
    }
    // QUIT (or PLAY) at the header's right, last in the keys' order.
    if (screen_ == kFiles) add(kBack, {W - 24 - 110, 20, 110, 32}, o_.play_button && !o_.in_use.empty() ? "PLAY" : "QUIT");
  }
  std::string labels;
  for (const Button& b : buttons_) labels += (labels.empty() ? "" : ", ") + b.label;
  say("screen %s: %s", name, labels);
  say("header %s", t_.header.empty() ? upper(g_.title) : t_.header);
  say("status %s", status_.empty() ? "(none)" : status_);
  if (screen_ == kFiles && !small_ && !o_.notice.empty()) say("notice %s", o_.notice);
  hot_ = focus_ = -1;
}

void Screen::blit_png(SDL_Texture* t, const SDL_FRect& d) { SDL_RenderTexture(ren_, t, nullptr, &d); }

void Screen::plate(const Button& b, bool hot) {
  const SDL_FRect r{static_cast<float>(b.r.x), static_cast<float>(b.r.y), static_cast<float>(b.r.w), static_cast<float>(b.r.h)};
  auto fill = [&](SDL_Color c, const SDL_FRect& f) {
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
    SDL_RenderFillRect(ren_, &f);
  };
  auto edge = [&](SDL_Color c) {
    SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
    SDL_RenderRect(ren_, &r);
  };
  const int cx = b.r.x + b.r.w / 2, cy = b.r.y + b.r.h / 2;
  int big = b.r.h >= 50 ? 20 : b.r.h >= 38 ? 17 : 16;
  while (big > 11 && title_.width(b.label, big) > b.r.w - 16) --big;  // a wide font's label fits its button
  if (b.style == kLink) {  // words in the accent, underlined
    const SDL_Color c = hot ? mix(t_.accent, t_.text, 1, 2) : t_.accent;
    title_.draw(ren_, b.label, big, c, b.r.x, cy, -1);
    fill(c, {static_cast<float>(b.r.x), cy + big / 2.0f + 2, static_cast<float>(title_.width(b.label, big)), 1});
    return;
  }
  SDL_Color words = !b.enabled ? t_.dim : hot ? t_.accent : t_.plate_text;
  if (b.style == kPrimary && b.enabled) {
    fill(hot ? mix(t_.accent, SDL_Color{255, 255, 255, 255}, 1, 5) : t_.accent, r);
    words = t_.on_accent;
  } else if (plate_) {
    const Uint8 m = !b.enabled ? 90 : hot ? 255 : 215;
    SDL_SetTextureColorMod(plate_, m, m, m);
    if (t_.plate_slice > 0) {
      const float s = static_cast<float>(t_.plate_slice);
      SDL_RenderTexture9Grid(ren_, plate_, nullptr, s, s, s, s, 1.0f, &r);
    } else {
      SDL_RenderTexture(ren_, plate_, nullptr, &r);
    }
    if (hot) edge(t_.accent);
  } else {
    fill(b.enabled ? (hot ? mix(t_.plate_colour, t_.plate_edge, 1, 3) : t_.plate_colour) : t_.plate_off, r);
    edge(hot ? t_.accent : t_.plate_edge);
  }
  if (b.sub.empty()) {
    title_.draw(ren_, b.label, big, words, cx, cy + 1);
  } else {
    title_.draw(ren_, title_.fit(b.label, big, b.r.w - 20), big, words, cx, cy - 10);
    body_.draw(ren_, body_.fit(b.sub, 13, b.r.w - 20), 13, b.enabled ? t_.dim : t_.plate_off, cx, cy + 10);
  }
}

// The band across the top: the badge and the game's title (and the setup window's second line).
void Screen::draw_head(int w, int h, int badge, int size) {
  const SDL_FRect top{0, 0, static_cast<float>(w), static_cast<float>(h)};
  if (band_) {
    blit_png(band_, top);
  } else {
    SDL_SetRenderDrawColor(ren_, t_.band_colour.r, t_.band_colour.g, t_.band_colour.b, 255);
    SDL_RenderFillRect(ren_, &top);
  }
  SDL_SetRenderDrawColor(ren_, t_.rule.r, t_.rule.g, t_.rule.b, 255);
  const SDL_FRect line{0, static_cast<float>(h), static_cast<float>(w), 1};
  SDL_RenderFillRect(ren_, &line);
  const std::string name = t_.header.empty() ? upper(g_.title) : t_.header;
  int x = 24;
  if (badge_) blit_png(badge_, {24.0f, (h - badge) / 2.0f, static_cast<float>(badge), static_cast<float>(badge)}), x += badge + 14;
  const int room = w - x - (small_ ? 24 : 160);
  while (size > 14 && title_.width(name, size) > room) --size;
  if (small_) {
    title_.draw(ren_, name, size, t_.text, x, h / 2, -1);
  } else {
    title_.draw(ren_, name, size, t_.text, x, h / 2 - 9, -1);
    title_.draw(ren_, o_.in_use.empty() ? "SETUP  \xC2\xB7  THE GAME NEEDS ITS ORIGINAL FILES" : "SETUP  \xC2\xB7  GAME FILES", 13,
                t_.accent, x + 1, h / 2 + 15, -1);
  }
}

void Screen::panel(int y, int h, const char* label) {
  const SDL_FRect r{static_cast<float>(kCx), static_cast<float>(y), static_cast<float>(kCw), static_cast<float>(h)};
  SDL_SetRenderDrawColor(ren_, t_.panel.r, t_.panel.g, t_.panel.b, 255);
  SDL_RenderFillRect(ren_, &r);
  SDL_SetRenderDrawColor(ren_, t_.rule.r, t_.rule.g, t_.rule.b, 255);
  SDL_RenderRect(ren_, &r);
  title_.draw(ren_, label, 14, t_.accent, kCx + 16, y + 18, -1);
}

void Screen::draw_files() {
  int y = 96;
  y += body_.draw_wrapped(ren_, find_words(), 15, t_.text, kCx, y, kCw, -1);
  if (!status_.empty()) body_.draw_wrapped(ren_, status_, 15, status_good_ ? t_.good : t_.bad, kCx, y + 6, kCw, -1);
  panel(166, 92, "FOUND ON THIS COMPUTER");
  if (!found_here_.empty()) {
    title_.draw(ren_, "A COPY OF " + upper(g_.title), 15, t_.ready, kCx + 16, 202, -1);
    body_.draw(ren_, body_.fit(found_here_, 14, kCw - 200, 3), 14, t_.dim, kCx + 16, 226, -1);
  } else {
    body_.draw(ren_, searched_ ? "No copy was found on this computer." : "SEARCH AGAIN looks on this computer.", 15, t_.dim,
               kCx + 16, 214, -1);
  }
  panel(270, 108, "USE MY OWN COPY");
  body_.draw(ren_, body_.fit("Pick " + what_to_pick() + ", or the folder it is installed in.", 15, kCw - 32), 15, t_.dim,
             kCx + 16, 308, -1);
  panel(390, 88, "DOWNLOAD THEM");
  if (!offer_.empty()) {
    const OriginalSource& o = offer_.front();
    title_.draw(ren_, "THE ORIGINAL GAME FILES", 15, t_.text, kCx + 16, 424, -1);
    body_.draw(ren_, body_.fit((o.note.empty() ? std::string("From the game's catalog") : "From " + o.note) + "  \xC2\xB7  " +
                                   detail::megabytes(o.size), 14, kCw - 220),
               14, t_.dim, kCx + 16, 448, -1);
  } else {
    body_.draw(ren_, catalog_asked_ ? "Asking the game's catalog..." : catalog_wanted_ ? "No download is available right now."
                                                                                        : "This game offers no download.",
               15, t_.dim, kCx + 16, 436, -1);
  }
  if (!o_.notice.empty()) body_.draw_wrapped(ren_, o_.notice, 14, t_.bad, kCx, 500, kCw, -1);
}

// A work's title, what it works on, the bar and its percent, the stage and the file.
void Screen::draw_progress(int x, int y, int w, bool small) {
  Progress p;
  std::string title, sub;
  if (work_) {
    std::lock_guard<std::mutex> g(work_->m);
    p = work_->progress, title = work_->title, sub = work_->sub;
  } else if (screen_ == kAsking) {
    title = "GETTING THE ORIGINAL GAME FILES", sub = "Asking the game's catalog...";
  }
  title_.draw(ren_, title_.fit(title, small ? 20 : 26, w), small ? 20 : 26, t_.accent, x, y, -1);
  body_.draw(ren_, body_.fit(sub, 14, w, 3), 14, t_.dim, x, y + 26, -1);
  const float f = static_cast<float>(std::clamp(p.fraction, 0.0, 1.0));
  const int bw = w - 56;
  const SDL_FRect bar{static_cast<float>(x), y + 46.0f, static_cast<float>(bw), 14}, in{x + 1.0f, y + 47.0f, (bw - 2) * f, 12};
  SDL_SetRenderDrawColor(ren_, t_.bg.r, t_.bg.g, t_.bg.b, 255);
  SDL_RenderFillRect(ren_, &bar);
  SDL_SetRenderDrawColor(ren_, t_.rule.r, t_.rule.g, t_.rule.b, 255);
  SDL_RenderRect(ren_, &bar);
  SDL_SetRenderDrawColor(ren_, t_.accent.r, t_.accent.g, t_.accent.b, 255);
  SDL_RenderFillRect(ren_, &in);
  char pct[16];
  std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(p.fraction * 100)));
  title_.draw(ren_, pct, 15, t_.text, x + w, y + 53, 1);
  body_.draw(ren_, body_.fit(p.stage, 15, w), 15, t_.text, x, y + 80, -1);
  body_.draw(ren_, body_.fit(p.message, 13, w), 13, t_.dim, x, y + 100, -1);
}

void Screen::draw() {
  const int w = lw(), h = lh();
  int pw = 0, ph = 0;
  SDL_GetCurrentRenderOutputSize(ren_, &pw, &ph);
  const float scale = std::max(1.0f, std::min(static_cast<float>(pw) / w, static_cast<float>(ph) / h));
  title_.set_scale(scale), body_.set_scale(scale);
  SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
  SDL_RenderClear(ren_);
  SDL_SetRenderDrawColor(ren_, t_.bg.r, t_.bg.g, t_.bg.b, 255);
  const SDL_FRect all{0, 0, static_cast<float>(w), static_cast<float>(h)};
  SDL_RenderFillRect(ren_, &all);
  if (background_) {  // scaled to cover
    float tw = 0, th = 0;
    SDL_GetTextureSize(background_, &tw, &th);
    const float k = std::max(w / tw, h / th);
    blit_png(background_, {(w - tw * k) / 2, (h - th * k) / 2, tw * k, th * k});
  }
  if (small_) {
    draw_head(w, 56, 32, 22);
    draw_progress(24, 86, w - 48, true);
  } else {
    draw_head(w, kHeadH, 44, 28);
    // The list of sections: GAME FILES, selected (the later sections come with the in-game menu).
    const SDL_FRect nav{0, kHeadH + 1.0f, kNavW, static_cast<float>(h - kHeadH - 1)}, sel{0, kHeadH + 13.0f, kNavW, 40},
        bar{0, kHeadH + 13.0f, 3, 40}, line{kNavW, kHeadH + 1.0f, 1, static_cast<float>(h - kHeadH - 1)};
    SDL_SetRenderDrawColor(ren_, t_.band_colour.r, t_.band_colour.g, t_.band_colour.b, 255);
    SDL_RenderFillRect(ren_, &nav);
    const SDL_Color s = t_.nav_on;
    SDL_SetRenderDrawColor(ren_, s.r, s.g, s.b, 255);
    SDL_RenderFillRect(ren_, &sel);
    SDL_SetRenderDrawColor(ren_, t_.accent.r, t_.accent.g, t_.accent.b, 255);
    SDL_RenderFillRect(ren_, &bar);
    SDL_SetRenderDrawColor(ren_, t_.rule.r, t_.rule.g, t_.rule.b, 255);
    SDL_RenderFillRect(ren_, &line);
    title_.draw(ren_, "GAME FILES", 16, t_.text, 20, kHeadH + 33, -1);
    switch (screen_) {
      case kFiles:
      case kAsking: draw_files(); break;
      case kConfirm: {
        title_.draw(ren_, "USE THESE GAME FILES?", 22, t_.accent, kCx, 108, -1);
        const int th = body_.draw_wrapped(ren_, confirm_text_, 15, t_.text, kCx, 216, kCw, -1);
        body_.draw(ren_, "Into " + body_.fit(confirm_into_, 15, kCw - 40, 3), 15, t_.accent, kCx, std::min(216 + th + 10, 312), -1);
        if (!status_.empty()) body_.draw_wrapped(ren_, status_, 15, status_good_ ? t_.good : t_.bad, kCx, 396, kCw, -1);
        break;
      }
      case kWorking:
        draw_progress(kCx, 116, kCw, false);
        body_.draw(ren_, "Cancel stops within a file and changes nothing.", 14, t_.dim, kCx, 392, -1);
        break;
    }
  }
  const int hot = focus_ >= 0 ? focus_ : hot_;
  for (size_t i = 0; i < buttons_.size(); ++i) plate(buttons_[i], static_cast<int>(i) == hot && buttons_[i].enabled);
  if (ask_pending_) {  // the frame left on screen while the system's picker comes up
    const SDL_FRect box{w / 2 - 150.0f, h - 42.0f, 300, 30};
    SDL_SetRenderDrawColor(ren_, t_.panel.r, t_.panel.g, t_.panel.b, 255);
    SDL_RenderFillRect(ren_, &box);
    SDL_SetRenderDrawColor(ren_, t_.accent.r, t_.accent.g, t_.accent.b, 255);
    SDL_RenderRect(ren_, &box);
    body_.draw(ren_, ask_folder_ ? "Opening the folder picker..." : "Opening the file picker...", 14, t_.accent, w / 2, h - 27);
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
  SDL_SetAudioStreamGain(audio_, s.gain);
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

// REKIT_SETUP_FIND's way (the screen before 2026-11-27): the search's copy looked at and confirmed, else the picker.
void Screen::search() {
  const std::string found = detail::search_copy(g_, o_.program_dir);
  searched_ = true, found_here_ = found;
  if (!found.empty()) return look_at(found);  // offered, to copy in or pass over
  status_ = "Not found on this computer. Pick it: " + what_to_pick() + ".", status_good_ = true;
  screen_ = kFiles, layout();
  ask_file(kFind);
}

// SEARCH AGAIN: FOUND ON THIS COMPUTER filled in, nothing else.
void Screen::search_here() {
  found_here_ = detail::search_copy(g_, o_.program_dir), searched_ = true;
  say("search: %s", found_here_.empty() ? "none" : found_here_);
  status_ = found_here_.empty() ? "No copy of " + g_.title + " was found on this computer."
                                : "A copy was found on this computer. USE THIS to use it.";
  status_good_ = true;
  layout();
}

void Screen::start_work(std::unique_ptr<Work> w, std::function<InstallResult(const ProgressFn&)> op) {
  work_ = std::move(w);
  say("work: %s", work_->title);
  work_->start(std::move(op));
  screen_ = kWorking, layout();
  if (!nav_install_.empty() && work_->kind != Work::kLook) {  // REKIT_SETUP_NAV_INSTALL: once an install or download runs
    const std::string all = nav_install_;
    nav_install_.clear();
    for (size_t at = 0; at <= all.size() && !done_;) {
      const size_t comma = std::min(all.find(',', at), all.size());
      const std::string s = all.substr(at, comma - at);
      nav(s == "next" ? 1 : s == "prev" ? -1 : s == "press" ? 2 : s == "back" ? 3 : 0);
      at = comma + 1;
    }
  }
}

// `file`: the picked file itself when `path` is the folder standing for it (only with the game's find hook).
void Screen::look_at(const std::string& path, const std::string& file) {
  found_ = std::make_shared<Found>();
  auto w = std::make_unique<Work>();
  w->kind = Work::kLook;
  w->title = "LOOKING AT YOUR GAME";
  w->sub = downloaded_.empty() ? path : download_sub();
  auto found = found_;
  const Game* game = &g_;  // the caller's, kept for the whole of run_setup
  start_work(std::move(w), [path, file, found, game](const ProgressFn& f) { return detail::look(*game, path, file, found.get(), f); });
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

// The game's catalog asked for its original sources, on a thread of its own (the screen is usable meanwhile).
void Screen::ask_catalog() {
  if (g_.original_catalogs.empty() || g_.original_key.empty() || g_.catalog_keys.empty()) return;
  catalog_wanted_ = true;
  std::vector<std::string> catalogs = g_.original_catalogs;
  if (kit_test_mode()) {  // a game's test run: no network (only a check's file:/// catalog)
    catalogs.erase(std::remove_if(catalogs.begin(), catalogs.end(),
                                  [&](const std::string& a) { return !allow_file_ || a.rfind("file:///", 0) != 0; }),
                   catalogs.end());
    if (catalogs.empty()) return say("original: not asked (RE_KIT_TEST: no network)");
  }
  catalog_asked_ = true;
  std::string error;
  original_dir_ = original_dir(o_.temp_dir, g_.id, &error);
  if (original_dir_.empty()) {
    catalog_why_ = error, catalog_done_ = true;
    return;
  }
  say("original: asking %s for %s", catalogs.front(), g_.original_key);
  const std::vector<std::string> keys = g_.catalog_keys;
  const std::string key = g_.original_key, dir = original_dir_, version = g_.version;
  const bool allow = allow_file_;
  catalog_th_ = std::thread([this, catalogs, keys, key, dir, allow, version] {
    std::vector<OriginalSource> got;
    std::string why;
    catalog_sources(catalogs, keys, key, dir, allow, [this](double) { return !stop_.load(); }, &got, &why, version);
    std::lock_guard<std::mutex> g(catalog_m_);
    catalog_answer_ = std::move(got), catalog_why_ = why;
    catalog_done_ = true;
  });
}

// The catalog's answer, once: its sources offered under DOWNLOAD THEM (downloading at once in the first start's
// small window), or why none are.
void Screen::offer_download() {
  catalog_asked_ = false;
  if (catalog_th_.joinable()) catalog_th_.join();
  {
    std::lock_guard<std::mutex> g(catalog_m_);
    offer_ = catalog_answer_;
    if (offer_.empty()) say("original: none offered: %s", catalog_why_);
  }
  const bool at_once = download_at_once_;
  download_at_once_ = false;
  if (offer_.empty()) {
    tidy_original_dir(original_dir_), original_dir_.clear();
    if (at_once && screen_ == kAsking) to_full("No copy of " + g_.title + " was found on this computer, and none can be downloaded now.", true);
    else if (screen_ == kFiles) layout();
    return;
  }
  say("original: %s sources offered", std::to_string(offer_.size()));
  if (at_once && screen_ == kAsking) return start_download();
  if (screen_ == kFiles) layout();
  if (auto_download_ && screen_ == kFiles && !work_) auto_download_ = false, activate(kDownload);
}

// "From the Internet Archive · 625 MB": the first source's, the one tried first.
std::string Screen::download_sub() const {
  if (offer_.empty()) return {};
  const OriginalSource& o = offer_.front();
  return (o.note.empty() ? std::string("From the game's catalog") : "From " + o.note) + "  \xC2\xB7  " + detail::megabytes(o.size);
}

// The fetched file gone (installed, refused or passed over): only ever the kit's own, in its marked folder.
void Screen::drop_download() {
  if (downloaded_.empty()) return;
  std::error_code ec;
  fs::remove(downloaded_, ec);
  say("original: %s deleted", fs::path(downloaded_).filename().string());
  downloaded_.clear();
}

void Screen::start_download() {
  std::string error;
  if (original_dir_.empty() && (original_dir_ = original_dir(o_.temp_dir, g_.id, &error)).empty()) {
    say("original: not downloaded: %s", error);
    const std::string why = "The game files could not be downloaded (" + error + ").";
    if (small_) return to_full(why, false);
    status_ = why, status_good_ = false;
    return layout();
  }
  drop_download();
  auto w = std::make_unique<Work>();
  w->kind = Work::kDownload;
  w->title = "GETTING THE ORIGINAL GAME FILES";
  w->sub = download_sub();
  const std::vector<OriginalSource> sources = offer_;
  const std::string dir = original_dir_;
  const bool allow = allow_file_;
  start_work(std::move(w), [sources, dir, allow](const ProgressFn& f) { return fetch_original(sources, dir, allow, f); });
}

void Screen::size_window() {
  const int w = small_ ? kSmallW : std::max(o_.width, 640), h = small_ ? kSmallH : std::max(o_.height, 360);
  SDL_SetWindowResizable(win_, !small_);
  SDL_SetWindowSize(win_, w, h);
  SDL_SetWindowPosition(win_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_SetRenderLogicalPresentation(ren_, lw(), lh(), SDL_LOGICAL_PRESENTATION_LETTERBOX);
  say("window %s", small_ ? "small" : "full");
}

void Screen::to_small() {
  small_ = true;
  if (win_) size_window();
}

// The first start's small window turned into the setup window, on GAME FILES, saying why.
void Screen::to_full(const std::string& status, bool good) {
  const bool was = small_;
  small_ = false, own_copy_ = false, use_found_ = false;
  if (was) size_window();
  status_ = status, status_good_ = good;
  screen_ = kFiles, layout();
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
  const bool own = own_copy_;
  const std::string pick_own = "Pick your own copy: " + what_to_pick() + ".";
  if (kind == Work::kDownload) {
    if (!r.ok || own) {
      say("original: not downloaded: %s", own ? std::string("the player's own copy instead") : r.summary);
      if (r.ok) downloaded_ = r.install_dir, drop_download();
      if (own) return to_full(pick_own, true);
      return to_full(r.cancelled ? r.summary : "The game files could not be downloaded (" + r.summary + "). Use your own copy instead.",
                     r.cancelled);
    }
    downloaded_ = r.install_dir;
    say("original: downloaded %s", r.summary);
    return picked(kFind, downloaded_);  // looked at as a pick of that file
  }
  if (!r.summary.empty()) say("%s", r.summary);
  if (kind == Work::kInstall) {
    drop_download();
    if (r.ok) {
      result_.exit = kSetupReady, result_.install_dir = r.install_dir.empty() ? out_dir() : r.install_dir;
      done_ = true;
      return;
    }
    return to_full(own ? pick_own : r.summary, own || r.cancelled);
  }
  const Found& f = *found_;
  const bool was_found = looking_found_;
  looking_found_ = false;
  if (!r.ok || own) {
    drop_download();
    if (was_found && !r.ok && !r.cancelled) found_here_.clear();  // not offered again
    return to_full(own ? pick_own : r.summary, own || r.cancelled);
  }
  std::error_code same;
  if (!o_.same_words.empty() && !o_.in_use.empty() && f.on_disk && fs::equivalent(f.path, o_.in_use, same))
    return to_full(o_.same_words, true);
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
  status_.clear();  // the files screen's ask ("Not found ... Pick it") is answered
  // A download's find, the first start's copy and USE THIS: installed at once into the default place (the choice
  // was the go; lead, 2026-11-26 and 2026-11-27), unless the copy has something to say (asks_first: files missing or
  // damaged, an unknown edition), which the confirm shows in the setup window. Another folder: a pick, then CHANGE
  // FOLDER.
  const bool asks = use_found_ && downloaded_.empty() && (g_.asks_first ? g_.asks_first(f) : f.version.empty());
  if (asks) say("found: it asks first");
  if (!downloaded_.empty() || (use_found_ && !asks)) {
    use_found_ = false;
    return activate(kGo);
  }
  use_found_ = false;
  if (small_) small_ = false, size_window();
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
    case kSearch: search_here(); break;
    case kUseFound:
      use_found_ = looking_found_ = true;
      look_at(found_here_);
      break;
    case kPickFile:
    case kPickFolder:
      status_ = "Pick it: " + what_to_pick() + ".", status_good_ = true;
      if (id == kPickFile) ask_file(kFind);
      else ask_folder(kFind);
      layout();
      break;
    case kDownload: start_download(); break;
    case kOwnCopy:
      if (work_) {
        own_copy_ = true, work_->cancel = true;
      } else {  // asking the catalog: its answer still comes, under DOWNLOAD THEM
        download_at_once_ = false;
        to_full("Pick your own copy: " + what_to_pick() + ".", true);
      }
      break;
    case kPickOther:
      drop_download();
      status_ = "Pick the copy to use: " + what_to_pick() + ".", status_good_ = true;
      screen_ = kFiles, layout();
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
        say("%s", "Nothing was copied: this game cannot install from here.");
        to_full("Nothing was copied: this game cannot install from here.", false);
        break;
      }
      auto w = std::make_unique<Work>();
      w->kind = Work::kInstall;
      w->title = "INSTALLING " + upper(g_.title);
      const auto found = found_;
      const std::string out = out_dir();
      w->sub = !downloaded_.empty() ? download_sub() : "From " + found->path;
      const std::string media = g_.media ? g_.media(out, o_.user_folder) : media_dir(out, o_.user_folder);
      auto install = g_.install;
      start_work(std::move(w), [found, out, media, install](const ProgressFn& f) { return install(*found, out, media, f); });
      break;
    }
    case kCancel:
      if (work_) work_->cancel = true;
      else if (screen_ == kAsking) download_at_once_ = false, to_full("No copy of " + g_.title + " was found on this computer.", true);
      break;
    case kBack:
      if (screen_ == kConfirm) {
        drop_download();
        into_.clear(), status_.clear(), screen_ = kFiles, layout();
      } else if (screen_ == kFiles) {
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
    activate(screen_ == kWorking || screen_ == kAsking ? kCancel : kBack);
  }
}

// The theme's fonts, art and sounds; what it leaves out, the kit's (the launcher's).
void Screen::load_look() {
  auto fonts = [&](detail::Text& text, bool body, const char* kit) {
    const std::string& want = body ? (t_.font_body.empty() ? t_.font : t_.font_body) : t_.font;
    std::error_code ec;
    if (!want.empty() && !fs::is_regular_file(want, ec)) say("theme: %s could not be read", want);
    const std::vector<unsigned char> bytes = theme_font(t_, body);
    const bool own = !want.empty() && fs::is_regular_file(want, ec);
    if (!text.open_memory(bytes.data(), bytes.size())) say("font %s: it could not be read, its text will be blank", own ? want : kit);
    else say("font %s", own ? want : kit);
  };
  fonts(title_, false, "the kit's Saira Condensed Bold");
  fonts(body_, true, "the kit's Saira Condensed Medium");
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
  auto wav = [&](bool hover, Sound* s) {
    const std::string& path = hover ? t_.hover_wav : t_.click_wav;
    std::error_code ec;
    if (!path.empty() && !fs::is_regular_file(path, ec)) say("theme: %s could not be read", path);
    const std::vector<unsigned char> bytes = theme_sound(t_, hover);
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!io || !SDL_LoadWAV_IO(io, true, &s->spec, &s->buf, &s->len)) say("theme: %s could not be read", path.empty() ? "a kit sound" : path);
    s->gain = hover ? 0.7f : 1.0f;  // as the launcher plays them
  };
  wav(false, &click_), wav(true, &hover_);
}

// REKIT_SETUP_PREVIEW: a state held for a review's picture, with made-up numbers (nothing read, fetched or copied).
void Screen::preview(const std::string& what) {
  say("preview %s", what);
  OriginalSource o;
  o.note = "the Internet Archive", o.size = 625000000;
  if (what == "files") {
    found_here_ = (fs::path("C:/Games") / g_.title).make_preferred().string(), searched_ = true;
    offer_ = {o};
    status_ = "No game files are set up yet. Use the copy found here, pick your own, or download them.", status_good_ = true;
    return layout();
  }
  to_small();
  if (what == "asking") return screen_ = kAsking, layout();
  auto w = std::make_unique<Work>();
  const bool copy = what == "copy";
  w->kind = copy ? Work::kInstall : Work::kDownload;
  w->title = copy ? "INSTALLING " + upper(g_.title) : "GETTING THE ORIGINAL GAME FILES";
  if (!copy) offer_ = {o};
  w->sub = copy ? "From " + (fs::path("C:/Games") / g_.title).make_preferred().string() : download_sub();
  const Progress p = copy ? Progress{"Copying the game files", 0.42, "data/zbd/mechlib.zbd"}
                          : Progress{"Downloading", 0.42, "263 of 625 MB  \xC2\xB7  4.1 MB/s"};
  w->progress = p;  // drawn from the first frame
  start_work(std::move(w), [p](const ProgressFn& f) {
    while (f(p)) SDL_Delay(5);
    InstallResult r;
    r.cancelled = true, r.summary = "Stopped. Nothing was changed.";
    return r;
  });
}

SetupResult Screen::run() {  // SDL's video is started (run_setup)
  say("video %s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)");
  auto_go_ = SDL_getenv("REKIT_SETUP_GO") != nullptr;
  pick_other_ = SDL_getenv("REKIT_SETUP_PICK_OTHER") != nullptr;
  auto_download_ = SDL_getenv("REKIT_SETUP_DOWNLOAD") != nullptr;
  allow_file_ = SDL_getenv("REKIT_SETUP_ALLOW_FILE") != nullptr;
  if (const char* k = SDL_getenv("REKIT_SETUP_NAV_INSTALL")) nav_install_ = k;
  if (const char* p = SDL_getenv("REKIT_SETUP_PREVIEW")) preview_ = p;
  const bool find = SDL_getenv("REKIT_SETUP_FIND") != nullptr;
  // A fresh setup (no files in use, nothing to look at first, nothing to say about them): the automatic first start.
  const bool fresh = o_.in_use.empty() && o_.look_first.empty() && o_.notice.empty() && !find && preview_.empty();
  if (fresh) {
    found_here_ = detail::search_copy(g_, o_.program_dir), searched_ = true;
    say(found_here_.empty() ? "start: no copy found on this computer" : "start: a copy found at %s", found_here_);
  }
  if (preview_.empty()) ask_catalog();
  small_ = fresh && (!found_here_.empty() || catalog_asked_);
  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | (small_ ? 0 : SDL_WINDOW_RESIZABLE);
  if (o_.hidden) flags |= SDL_WINDOW_HIDDEN;
  win_ = SDL_CreateWindow(g_.title.c_str(), small_ ? kSmallW : std::max(o_.width, 640), small_ ? kSmallH : std::max(o_.height, 360), flags);
  // Hidden (a check): SDL's software renderer, the same pixels everywhere and no graphics driver loaded.
  if (!win_ || !(ren_ = SDL_CreateRenderer(win_, o_.hidden ? SDL_SOFTWARE_RENDERER : nullptr))) {
    result_.exit = kSetupFailed, result_.error = std::string("no window: ") + SDL_GetError();
    return result_;
  }
  SDL_SetRenderLogicalPresentation(ren_, lw(), lh(), SDL_LOGICAL_PRESENTATION_LETTERBOX);
  say("window %s", small_ ? "small" : "full");
  if (!o_.hidden) SDL_SetRenderVSync(ren_, 1);
  load_look();
  const bool pads = !o_.hidden && SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  if (!o_.in_use.empty())
    status_ = "In use: " + o_.in_use + (o_.play_button ? ". Choose other files below, or PLAY to go on." : "."), status_good_ = true;
  const char* frames_env = SDL_getenv("REKIT_SETUP_FRAMES");
  const long frames_max = frames_env ? std::atol(frames_env) : 0;
  const char* shot = SDL_getenv("REKIT_SETUP_SHOT");
  if (!preview_.empty()) {
    preview(preview_);
  } else if (!o_.look_first.empty()) {
    layout();
    look_at(o_.look_first);
  } else if (find) {
    layout();
    activate(kFind);
  } else if (small_ && !found_here_.empty()) {  // the first start: the copy found used at once
    use_found_ = looking_found_ = true;
    look_at(found_here_);
  } else if (small_) {  // none found: the catalog's download, once it answers
    download_at_once_ = true;
    screen_ = kAsking, layout();
  } else {
    if (fresh) status_ = "No copy of " + g_.title + " was found on this computer.", status_good_ = true;
    layout();
  }
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
      const bool busy = screen_ == kWorking || screen_ == kAsking;
      if (busy && e.type != SDL_EVENT_MOUSE_BUTTON_UP && e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_MOUSE_MOTION &&
          e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN)
        continue;
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
        nav(a);
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
    if (catalog_asked_ && catalog_done_) offer_download();
    if (done_) break;
    draw();
    // A preview's frames count while its made-up work holds; else only those drawn with nothing at work.
    const bool counting = !preview_.empty() || (!work_ && !catalog_asked_);
    const bool last = frames_max <= 0 || (counting && frames + 1 >= frames_max);
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
    if (counting) ++frames;
    else if (o_.hidden) SDL_Delay(1);
    if (frames_max > 0 && frames >= frames_max) {
      say("frames %s: ended", std::to_string(frames));
      if (work_) work_->cancel = true;
      done_ = true;
    }
    if (!o_.hidden && !work_ && !asking_ && !catalog_asked_) SDL_WaitEventTimeout(nullptr, 16);
    else if (!o_.hidden) SDL_Delay(16);
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
