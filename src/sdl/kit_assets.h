#pragma once
// The kit's default look built into the library (assets/README.md; cmake/embed_assets.cmake makes the bytes).
#include <cstddef>

namespace rekit::sdl::detail {

struct Asset {
  const unsigned char* data = nullptr;
  size_t size = 0;
};

Asset kit_font_bold();  // Saira Condensed Bold: titles, buttons, labels
Asset kit_font_body();  // Saira Condensed Medium: sentences, paths, progress
Asset kit_click_wav();  // a button pressed
Asset kit_hover_wav();  // a button under the pointer

}  // namespace rekit::sdl::detail
