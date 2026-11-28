#pragma once
// The setup screen's text (internal to the kit): one TrueType font through stb_truetype, each string rasterised at
// the window's pixel density (so it is sharp on a Retina or HiDPI screen) and drawn at the screen's logical size.
// Sizes are in logical pixels per em, as SDL_ttf's point sizes are at 72 dpi.
#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rekit::sdl::detail {

class Text {
 public:
  Text();
  ~Text();
  Text(const Text&) = delete;
  Text& operator=(const Text&) = delete;

  // A .ttf or .otf (a .ttc's first face). False: no text is drawn.
  bool open(const std::string& path);
  // The same from bytes in memory (the kit's own fonts, kit_assets.h), copied.
  bool open_memory(const unsigned char* data, size_t size);
  bool ok() const { return ok_; }
  // Physical pixels per logical pixel; a change drops the cached strings.
  void set_scale(float scale);

  int width(const std::string& s, int size);
  int line_skip(int size);
  // `y` is the line's middle; `align` -1 left of x, 0 centred on x, 1 right of x.
  void draw(SDL_Renderer* r, const std::string& s, int size, SDL_Color c, int x, int y, int align = 0);
  // Words wrapped to `width`, each line centred on `x` (`align` -1: starting at `x`), the first line's middle at
  // `y`; the height used.
  int draw_wrapped(SDL_Renderer* r, const std::string& s, int size, SDL_Color c, int x, int y, int width, int align = 0);
  std::vector<std::string> wrap(const std::string& s, int size, int width);
  // `s` shortened in the middle with "..." to fit `width`, its first `keep` bytes kept whole.
  std::string fit(const std::string& s, int size, int width, size_t keep = 0);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool ok_ = false;
};

// A font every system has (Segoe UI or Arial on Windows, Helvetica or Arial on the Mac, DejaVu Sans or Liberation
// Sans on Linux); "" when none is found.
std::string system_font();

// A PNG read with stb_image into an SDL surface (ABGR8888 bytes); null when it cannot be read.
SDL_Surface* load_png(const std::string& path);

}  // namespace rekit::sdl::detail
