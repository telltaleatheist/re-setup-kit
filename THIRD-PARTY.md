# Third-party code in the kit

A game that ships the kit ships these notices with it (its package's
third-party file): each library's licence text is beside its code.

| code | used for | licence | text |
| --- | --- | --- | --- |
| miniz 3.0.2 (inflate only) | ZIP and raw deflate | MIT | `third_party/miniz/LICENSE` |
| stb_truetype 1.28 | the setup screen's text | public domain or MIT | `third_party/stb/LICENSE` |
| stb_image 2.34 (PNG only) | a theme's PNG art | public domain or MIT | `third_party/stb/LICENSE` |

The kit also links, when the game provides them, SDL3 (zlib licence),
libarchive (BSD 2-clause), zlib (zlib licence) and liblzma (0BSD); their
notices are the game's to ship with the libraries it builds.

The setup screen draws in the game's theme font, or else a font the system
already has (Segoe UI or Arial, Helvetica or Arial, DejaVu Sans or Liberation
Sans); it carries no font of its own.
