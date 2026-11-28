# Third-party code in the kit

A game that ships the kit ships these notices with it (its package's
third-party file): each library's licence text is beside its code.

| code | used for | licence | text |
| --- | --- | --- | --- |
| miniz 3.0.2 (inflate only) | ZIP and raw deflate | MIT | `third_party/miniz/LICENSE` |
| stb_truetype 1.28 | the setup screen's text | public domain or MIT | `third_party/stb/LICENSE` |
| stb_image 2.34 (PNG only) | a theme's PNG art | public domain or MIT | `third_party/stb/LICENSE` |
| Monocypher 4.0.2 (with its optional Ed25519) | checking signed catalogs and SHA256SUMS | CC0 or BSD 2-clause | `third_party/monocypher/LICENCE.md` |
| Saira Condensed Bold and Medium (the Saira Project Authors) | the setup screen's default text (the launcher's look) | SIL Open Font License 1.1 | `assets/OFL.txt` |

The kit also links, when the game provides them, SDL3 (zlib licence),
libarchive (BSD 2-clause), zlib (zlib licence) and liblzma (0BSD); their
notices are the game's to ship with the libraries it builds.

The setup screen draws in the game's theme font, or else the launcher's
(Saira Condensed, built into `rekit_sdl` from `assets/`; 2026-11-27). Its
default sounds (`assets/click.wav`, `hover.wav`) are the project's own.

The font's licence is the one text a game's package needs that the kit
does not compile in: CMake lists it in `REKIT_NOTICE_FILES` (a cached
variable set when `rekit_sdl` is built), and each game's packaging copies
every file named there beside its own notices.
