#include "setup_text.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

// stb's own code, static in this file (a game's imgui keeps its copy static too), its warnings not ours.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb/stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace rekit::sdl::detail {
namespace {

// UTF-8 to code points; a bad byte reads as U+FFFD.
std::vector<uint32_t> code_points(const std::string& s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    if (n < 0 || (n > 0 && i + n >= s.size())) {  // not a lead byte, or the sequence runs past the end
      out.push_back(0xFFFD), ++i;
      continue;
    }
    uint32_t cp = n == 0 ? c : c & (0x3F >> n);
    bool bad = false;
    for (int k = 1; k <= n; ++k) {
      const unsigned char d = static_cast<unsigned char>(s[i + k]);
      if ((d >> 6) != 2) bad = true;
      cp = (cp << 6) | (d & 0x3F);
    }
    out.push_back(bad ? 0xFFFD : cp);
    i += bad ? 1 : static_cast<size_t>(n) + 1;
  }
  return out;
}

std::vector<unsigned char> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

struct Text::Impl {
  std::vector<unsigned char> bytes;
  stbtt_fontinfo font{};
  float scale = 1.0f;
  int ascent = 0, descent = 0, gap = 0;
  struct Key {
    std::string s;
    int size;
    bool operator<(const Key& o) const { return std::tie(size, s) < std::tie(o.size, o.s); }
  };
  struct Entry {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;  // physical pixels
    std::list<Key>::iterator age;
  };
  std::map<Key, Entry> cache;
  std::list<Key> ages;  // the newest first
  SDL_Renderer* renderer = nullptr;
  static constexpr size_t kKeep = 256;

  float em(int size) const { return stbtt_ScaleForMappingEmToPixels(&font, static_cast<float>(size) * scale); }
  // The string's width in physical pixels at `size`.
  float advance(const std::vector<uint32_t>& cps, int size) const {
    const float k = em(size);
    float x = 0;
    for (size_t i = 0; i < cps.size(); ++i) {
      int adv = 0, lsb = 0;
      stbtt_GetCodepointHMetrics(&font, static_cast<int>(cps[i]), &adv, &lsb);
      x += adv * k;
      if (i + 1 < cps.size()) x += k * stbtt_GetCodepointKernAdvance(&font, static_cast<int>(cps[i]), static_cast<int>(cps[i + 1]));
    }
    return x;
  }
  void clear() {
    for (auto& [k, e] : cache)
      if (e.tex) SDL_DestroyTexture(e.tex);
    cache.clear(), ages.clear();
  }
  Entry* get(SDL_Renderer* r, const std::string& s, int size) {
    if (r != renderer) clear(), renderer = r;
    const Key key{s, size};
    if (auto it = cache.find(key); it != cache.end()) {
      ages.splice(ages.begin(), ages, it->second.age);
      return &it->second;
    }
    const std::vector<uint32_t> cps = code_points(s);
    const float k = em(size);
    const int w = std::max(1, static_cast<int>(std::ceil(advance(cps, size))) + 2);
    const int top = static_cast<int>(std::ceil(ascent * k)), h = std::max(1, top + static_cast<int>(std::ceil(-descent * k)) + 1);
    std::vector<unsigned char> alpha(static_cast<size_t>(w) * h, 0);
    float x = 1;
    for (size_t i = 0; i < cps.size(); ++i) {
      const int cp = static_cast<int>(cps[i]);
      int adv = 0, lsb = 0, x0, y0, x1, y1;
      stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
      const float frac = x - std::floor(x);
      stbtt_GetCodepointBitmapBoxSubpixel(&font, cp, k, k, frac, 0, &x0, &y0, &x1, &y1);
      const int gx = static_cast<int>(std::floor(x)) + x0, gy = top + y0, gw = x1 - x0, gh = y1 - y0;
      if (gw > 0 && gh > 0 && gx >= 0 && gy >= 0 && gx + gw <= w && gy + gh <= h) {
        std::vector<unsigned char> g(static_cast<size_t>(gw) * gh);
        stbtt_MakeCodepointBitmapSubpixel(&font, g.data(), gw, gh, gw, k, k, frac, 0, cp);
        for (int yy = 0; yy < gh; ++yy)
          for (int xx = 0; xx < gw; ++xx) {
            unsigned char& d = alpha[static_cast<size_t>(gy + yy) * w + gx + xx];
            d = static_cast<unsigned char>(std::min(255, d + g[static_cast<size_t>(yy) * gw + xx]));
          }
      }
      x += adv * k;
      if (i + 1 < cps.size()) x += k * stbtt_GetCodepointKernAdvance(&font, cp, static_cast<int>(cps[i + 1]));
    }
    SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return nullptr;
    for (int yy = 0; yy < h; ++yy) {
      auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(surf->pixels) + static_cast<size_t>(yy) * surf->pitch);
      for (int xx = 0; xx < w; ++xx) row[xx] = (uint32_t(alpha[static_cast<size_t>(yy) * w + xx]) << 24) | 0x00FFFFFFu;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    if (cache.size() >= kKeep) {  // the oldest goes
      const Key old = ages.back();
      ages.pop_back();
      if (auto it = cache.find(old); it != cache.end()) SDL_DestroyTexture(it->second.tex), cache.erase(it);
    }
    ages.push_front(key);
    Entry& e = cache[key];
    e.tex = tex, e.w = w, e.h = h, e.age = ages.begin();
    return &e;
  }
};

Text::Text() : impl_(std::make_unique<Impl>()) {}
Text::~Text() { impl_->clear(); }

bool Text::open(const std::string& path) {
  if (path.empty()) return ok_ = false;
  const std::vector<unsigned char> bytes = read_file(path);
  return open_memory(bytes.data(), bytes.size());
}

bool Text::open_memory(const unsigned char* data, size_t size) {
  ok_ = false;
  impl_->clear();
  impl_->bytes.assign(data, data + size);
  if (impl_->bytes.empty()) return false;
  const int offset = stbtt_GetFontOffsetForIndex(impl_->bytes.data(), 0);
  if (offset < 0 || !stbtt_InitFont(&impl_->font, impl_->bytes.data(), offset)) return false;
  stbtt_GetFontVMetrics(&impl_->font, &impl_->ascent, &impl_->descent, &impl_->gap);
  return ok_ = true;
}

void Text::set_scale(float scale) {
  if (scale <= 0 || std::fabs(scale - impl_->scale) < 0.001f) return;
  impl_->scale = scale;
  impl_->clear();
}

int Text::width(const std::string& s, int size) {
  if (!ok_) return 0;
  return static_cast<int>(std::ceil(impl_->advance(code_points(s), size) / impl_->scale));
}

int Text::line_skip(int size) {
  if (!ok_) return size;
  const float k = stbtt_ScaleForMappingEmToPixels(&impl_->font, static_cast<float>(size));
  return static_cast<int>(std::ceil((impl_->ascent - impl_->descent + impl_->gap) * k));
}

void Text::draw(SDL_Renderer* r, const std::string& s, int size, SDL_Color c, int x, int y, int align) {
  if (!ok_ || s.empty()) return;
  Impl::Entry* e = impl_->get(r, s, size);
  if (!e) return;
  const float w = e->w / impl_->scale, h = e->h / impl_->scale;
  const float k = stbtt_ScaleForMappingEmToPixels(&impl_->font, static_cast<float>(size));
  // The texture's top is the font's ascent above the baseline; the line's middle (halfway from the ascent to the
  // descent) goes at y.
  const float top = static_cast<float>(y) - (impl_->ascent - impl_->descent) * k / 2.0f;
  const float left = align < 0 ? static_cast<float>(x) : align == 0 ? x - w / 2.0f : x - w;
  // Snapped to whole physical pixels, so a glyph's edges stay sharp.
  SDL_FRect dst{std::round(left * impl_->scale) / impl_->scale, std::round(top * impl_->scale) / impl_->scale, w, h};
  SDL_SetTextureColorMod(e->tex, c.r, c.g, c.b);
  SDL_SetTextureAlphaMod(e->tex, c.a);
  SDL_RenderTexture(r, e->tex, nullptr, &dst);
}

std::vector<std::string> Text::wrap(const std::string& s, int size, int width) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flush_word = [&] {
    if (word.empty()) return;
    const std::string with = line.empty() ? word : line + " " + word;
    if (!line.empty() && this->width(with, size) > width) lines.push_back(line), line = word;
    else line = with;
    word.clear();
  };
  for (char ch : s) {
    if (ch == ' ') flush_word();
    else if (ch == '\n') flush_word(), lines.push_back(line), line.clear();
    else word += ch;
  }
  flush_word();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

int Text::draw_wrapped(SDL_Renderer* r, const std::string& s, int size, SDL_Color c, int x, int y, int width, int align) {
  const int skip = line_skip(size);
  const std::vector<std::string> lines = wrap(s, size, width);
  for (size_t i = 0; i < lines.size(); ++i) draw(r, lines[i], size, c, x, y + static_cast<int>(i) * skip, align);
  return static_cast<int>(lines.size()) * skip;
}

std::string Text::fit(const std::string& s, int size, int width, size_t keep) {
  if (!ok_ || this->width(s, size) <= width) return s;
  // Cut from the middle on whole UTF-8 characters: the start (at least `keep`) and the end stay.
  auto starts = [&](size_t i) { return i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80; };
  size_t head = std::max(keep, s.size() / 2), tail = s.size() - head;
  for (int guard = 0; guard < 4096; ++guard) {
    while (head > keep && !starts(head)) --head;
    while (tail < s.size() && !starts(s.size() - tail)) --tail;
    const std::string t = s.substr(0, head) + "..." + s.substr(s.size() - tail);
    if (this->width(t, size) <= width || (head <= keep && tail == 0)) return t;
    if (tail > 0 && (tail >= head - keep || head <= keep)) --tail;
    else if (head > keep) --head;
    else --tail;
  }
  return s.substr(0, keep) + "...";
}

std::string system_font() {
  std::vector<std::string> c;
#if defined(_WIN32)
  const char* windir = SDL_getenv("WINDIR");
  const std::string fonts = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\";
  c = {fonts + "segoeui.ttf", fonts + "arial.ttf", fonts + "tahoma.ttf"};
#elif defined(__APPLE__)
  c = {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf", "/System/Library/Fonts/Helvetica.ttc",
       "/System/Library/Fonts/Geneva.ttf"};
#else
  c = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
       "/usr/share/fonts/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
       "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-Regular.ttf",
       "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"};
#endif
  std::error_code ec;
  for (const std::string& p : c)
    if (std::filesystem::is_regular_file(p, ec)) return p;
  return "";
}

SDL_Surface* load_png(const std::string& path) {
  const std::vector<unsigned char> bytes = read_file(path);
  if (bytes.empty()) return nullptr;
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
  if (!px) return nullptr;
  SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
  if (s)
    for (int y = 0; y < h; ++y)
      std::copy(px + static_cast<size_t>(y) * w * 4, px + static_cast<size_t>(y + 1) * w * 4,
                static_cast<unsigned char*>(s->pixels) + static_cast<size_t>(y) * s->pitch);
  stbi_image_free(px);
  return s;
}

}  // namespace rekit::sdl::detail
