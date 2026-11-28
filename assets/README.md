# The kit's default look (2026-11-27)

The launcher's look, built into `rekit_sdl` (CMake makes `rekit_assets.cpp` from
these files) so every game's setup has it with no files of its own to ship. A
game's theme replaces any of them (`Theme::font`, `font_body`, `click_wav`,
`hover_wav`).

| file | what | from |
| --- | --- | --- |
| `SairaCondensed-Bold.ttf` | titles, buttons, labels | the hub's `assets/launcher/font.ttf`, unchanged |
| `SairaCondensed-Medium.ttf` | sentences, paths, progress | the hub's `assets/launcher/font_body.ttf`, unchanged |
| `OFL.txt` | the fonts' licence | the hub's `assets/launcher/font-OFL.txt` |
| `click.wav` | a button pressed | the hub's `assets/launcher/click.wav` |
| `hover.wav` | a button under the pointer, played at 0.7 | the hub's `assets/launcher/hover.wav` |

Saira Condensed is by the Saira Project Authors, under the SIL Open Font
License 1.1 (`OFL.txt`; "Saira" is its reserved name), from
github.com/google/fonts `ofl/sairacondensed`; THIRD-PARTY.md lists it.

The two sounds are the project's own, made for the launcher (2026-11-26) with
a local Stable Audio 3.0 Small SFX model; the hub's `assets/launcher/README.md`
has their prompts.
