#pragma once
// The kit's look, shared by the setup screen and the in-game menu (docs/plan-in-game-menu.md section 2). Every field
// is optional: what a game's theme leaves as it is is the launcher's look, the kit's default (2026-11-27; its fonts
// and sounds built into rekit_sdl, assets/README.md). A game's theme sets its title, art and any of these; its words
// come from its recipe.
#include <SDL3/SDL.h>

#include <cstddef>
#include <string>
#include <vector>

namespace rekit::sdl {

struct Theme {
  std::string header;      // the screens' title ("MECHWARRIOR 1"); else the game's title in capitals
  std::string font;        // titles, buttons, labels: a .ttf or .otf; else the kit's Saira Condensed Bold
  std::string font_body;   // sentences, paths, progress; else `font` when the game gives one, else Saira Condensed Medium
  std::string badge;       // a PNG with alpha, beside the header
  std::string background;  // a PNG, scaled to cover the window
  std::string band;        // a PNG for the header band, scaled; else the band colour
  std::string plate;       // a PNG for the plain buttons, nine-sliced by plate_slice (0: scaled whole)
  int plate_slice = 0;
  SDL_Color bg{11, 14, 19, 255};           // the window
  SDL_Color band_colour{17, 21, 28, 255};  // the header and the list of sections
  SDL_Color rule{60, 69, 82, 255};         // the lines between them and the panels' edges (#3c4552)
  SDL_Color panel{20, 25, 32, 255};        // a panel's inside
  SDL_Color text{244, 245, 247, 255}, dim{195, 201, 210, 255}, accent{242, 165, 31, 255};  // accent #f2a51f
  SDL_Color plate_colour{32, 38, 47, 255}, plate_off{24, 28, 34, 255};  // a button (#20262f); one that can't be pressed
  SDL_Color plate_edge{74, 83, 95, 255}, plate_text{233, 236, 240, 255};  // a button's edge and its words
  SDL_Color on_accent{20, 25, 32, 255};    // words on the accent (USE THIS, DOWNLOAD, INSTALL)
  SDL_Color good{223, 227, 232, 255}, bad{255, 150, 128, 255};  // a status sentence; one about a problem
  SDL_Color ready{127, 214, 154, 255};     // what a found copy is
  // The menu over a running game (step 2b): the dim over the game while it is open, the selected section's fill (its
  // bar in the accent), and an inset box inside a page (its edge in `rule`).
  SDL_Color scrim{5, 8, 12, 184}, nav_on{38, 45, 55, 255}, well{16, 21, 27, 255};
  std::string click_wav, hover_wav;        // a button pressed, under the pointer (at 0.7); else the kit's (the launcher's)
};

// A theme's font or sound as bytes: the game's file when the theme names one and it reads, else the kit's built-in
// one. `body` picks font_body (else the game's `font`, else Saira Condensed Medium); `hover` picks hover_wav.
std::vector<unsigned char> theme_font(const Theme& theme, bool body);
std::vector<unsigned char> theme_sound(const Theme& theme, bool hover);

}  // namespace rekit::sdl
