#include "rekit/sdl/theme.h"

#include <fstream>
#include <iterator>

#include "kit_assets.h"

namespace rekit::sdl {
namespace {

std::vector<unsigned char> file_or(const std::string& path, detail::Asset kit) {
  if (!path.empty()) {
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (f.good() || f.eof())
      if (!bytes.empty()) return bytes;
  }
  return std::vector<unsigned char>(kit.data, kit.data + kit.size);
}

}  // namespace

std::vector<unsigned char> theme_font(const Theme& t, bool body) {
  if (!body) return file_or(t.font, detail::kit_font_bold());
  return file_or(t.font_body.empty() ? t.font : t.font_body, detail::kit_font_body());
}

std::vector<unsigned char> theme_sound(const Theme& t, bool hover) {
  return hover ? file_or(t.hover_wav, detail::kit_hover_wav()) : file_or(t.click_wav, detail::kit_click_wav());
}

}  // namespace rekit::sdl
