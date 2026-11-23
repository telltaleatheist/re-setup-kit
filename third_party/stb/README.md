# stb (stb_truetype, stb_image)

Sean Barrett's single-file libraries, public domain or MIT (the reader's
choice; both texts are at the end of each header and in `LICENSE`). Unchanged copies from
https://github.com/nothings/stb at commit
6b5799bae8a1fe3792691ef4b72ae26434c3e799, sha256:

- `stb_truetype.h` (v1.28) 428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f
- `stb_image.h` (v2.34) dfc2aadda6295f67f677587773f25e5bf8c8ad427ed0db7c518525c91e72e020

The setup screen (`src/sdl/setup_text.cpp`, `src/sdl/setup_screen.cpp`)
draws its text with stb_truetype from the theme's font (or the system's) and
reads a theme's PNG art with stb_image (PNG only), so a game needs neither
SDL_ttf nor SDL_image.
