# re-setup-kit: design

The shared "find your game files" kit for the -re games (Owen, 2026-11-21:
one setup kit for Mac, Windows and Linux). Every game, started on its own,
finds or takes the player's original files the same way, and the launcher
drives the same code through each game, so there is one setup to maintain,
not one per game.

## What it is for

- A player starts any game (mech3-re, mech3-pm-re, mech1-remake-re,
  mech1-re, inception-re, ...) without the launcher. With no game files
  found it opens the setup screen: point at an installed copy, the disc or
  its drive, a disc image (.iso, .cue/.bin), or any of those inside an
  archive (.zip, .7z, .rar, .tar...), at any depth. Nothing is unpacked or
  installed by hand.
- From a disc, an image or an archive, the game is installed into a folder
  the player chooses (default: the common games folder), never by running
  the original's Windows installer.
- The launcher asks each game what it has (`--status`) and asks it to set
  itself up (`--setup`). It never carries a game's recipe.

## The rule

The kit knows no game. Nothing in it names MechWarrior or Inception; a game
describes itself through its **recipe** (below), which lives in the game's
own repo. Anything that is true of every old PC game (archives, disc images,
CD audio, InstallShield cabinets, safe paths, free space, the system's
pickers) is the kit's.

That holds for what a player sees too (Owen, 2026-11-21: the same screen
serves MechWarrior 3, Pirate's Moon, MW1 and Inception). The setup screen
carries no "MW3 Rebuild" or any game's name, art or wording of its own: its
title, logo, background, font, colours and sounds come from the game's
**theme** (part of the recipe), and its sentences name the game through the
recipe ("Find MechWarrior 1's files", "Copied into ..."). With no theme it
draws plainly (a dark background, the system's font) and still works.

## Layers

1. **core** (C++20, no SDL; libarchive, zlib, liblzma):
   - sources: walking whatever was picked (folder, file, archive in archive,
     disc image) into a list of what it holds, read forward once;
   - archives (libarchive: zip/zip64, 7z, RAR4/5, tar and compressions,
     ISO 9660), raw CD sectors as 2048-byte data (`cooked_sectors`);
   - cue sheets and their CD audio as `trackNN.wav`;
   - InstallShield 5 cabinets (DATA1.HDR/.CAB), read in place, MD5-checked;
   - plain ZIP (self-extracting stubs too) and raw deflate;
   - staging: an install written to `<dir>.new` and renamed, a failure or a
     cancel leaving nothing; free-space checks; `safe_relative` paths;
   - the install search: registry values, default folders, Wine, Proton,
     Lutris, CrossOver and Whisky prefixes, beside the program;
   - the `--status` JSON and the `--setup` result.
2. **sdl** (SDL3): the system's file and folder pickers (never opened in a
   run no player is at), helper programs started with piped output, the
   setup screen itself (its own font and drawing, no game engine).
3. **recipe** (in each game's repo, against the kit's interface).

## The recipe

What a game tells the kit, roughly:

```cpp
namespace rekit {
struct Recipe {
  std::string id;      // "mech3", "mech3-pm", "mech1-remake", ...
  std::string title;   // "MechWarrior 3", for the screen's words
  // An installed copy: is this folder the game, which version, what is missing.
  std::function<Identity(const Folder&)> identify_install;
  // A disc or image: is this file list (an installer's, a disc's) the game.
  std::function<Identity(const FileList&)> identify_disc;
  // Installing a found disc into `out_dir` (most games: the kit's cabinet
  // reader plus a file list; a game may supply its own).
  std::function<Result(const Found&, const std::string& out_dir, const ProgressFn&)> install;
  // Where its installer wrote, for the search (registry keys, folder names).
  SearchHints search;
  // Work after the files are found, once: movies, a patch, our own data.
  std::vector<FirstRunStep> first_run;
  // Other games it needs (the MW1 remake: MechWarrior 3 and Pirate's Moon).
  std::vector<std::string> needs;
};
}
```

The setup screen's recipe (`rekit::sdl::Game`) may also carry `recognise`: when the
kit's walk finds no copy in what the player picked, the screen asks it what the pick
is, and shows its one sentence, as plain text, instead of "No copy of <title> was
found in <pick>." (Inception names its Amiga, C64 and Apple II copies this way, which
the kit can't read.) An empty answer, a throw or no hook keeps the kit's sentence.

`find` goes further: when the walk finds no copy, the screen asks it for one (a
`Found` the recipe's own `install` understands: Inception's Amiga disk image) and
offers it like any find. With it set, a picked file of no kind the kit knows is asked
about itself before its folder. `pick_words`, `find_sub` and `find_show` are the
screen's words for what to pick. A game that sets none of these has the screen as it
was.

## The launcher contract

Each game program answers:

- `game --status`: one JSON object on stdout, exit 0, nothing on screen.
  ```json
  {"kit": 1, "game": "mech3", "version": "1.2.2",
   "files": {"state": "ready", "where": "/path", "data": "1.2"},
   "first_run": {"pending": ["movies"]},
   "needs": []}
  ```
  `state` is `ready`, `missing` (nothing found) or `incomplete` (found, with
  `missing` listing what).
- `game --setup`: opens the setup screen; exit 0 when the game is ready,
  1 when the player cancelled, 2 on a failure (the reason on stdout as JSON).
- plain `game`: plays, opening the setup screen first when its files are
  missing.
- `game --setup --describe | --search | --look <pick> | --install <pick>
  [--folder <dir>]`: the same setup without a screen (`rekit/sdl/setup_serve.h`,
  2026-11-24), for a launcher that shows it on its own screens. JSON lines on
  stdout, `done` on the last; a `cancel` line on stdin stops it. The recipe
  stays the game's. A game that answers these says `"setup": 1` in its status
  (`Status::setup = kServedSetup`); a launcher starts any other plain, as
  before. The game saves the returned `install_dir` as after its setup screen.
- For a launcher that starts and sets up every game this way (2026-11-25), each
  optional and left out when a game has nothing to say:
  - the status's `notes` (sentences for the player before playing), `user`
    (the game's user folder) and `play` (`kPlayOptions`: it takes
    `--windowed`, `--window WxH`, `--nosound`, `--hidden`, `--write-dir` and
    `--ready-file`, so a launcher may close at its first frame);
  - `game --setup --copies` (a line for each copy of the game on this computer:
    its folder, data version, whether it is in use, what it lacks),
    `--add <pick>` (what a disc holds that the files in use lack, added) and
    `--first-run <step>` (a pending step done now, with progress). A recipe
    answers them through `Game::copies`, `add` and `first_run`; without the
    hook the answer is "not a setup this game answers".
  - (2026-11-26) the status's `notes_once` and `files.lacks`. `notes_once`: N
    means the first N `notes` are shown once until they change (the launcher
    keeps a hash of the N it showed), the rest every time; left out, 0, every
    note every time. `lacks`, with `ready`: what the files in use lack and play
    without, in the words a `--copies` row's `missing` uses ("music",
    "movies", ...), which a game fills from the same code. `missing` stays
    what keeps a game from being ready; the two are never mixed.
  `kStatusVersion` stays 1: no field changes meaning, and a launcher that knows
  none of these reads a game that sets them as before.

Settings stay each game's own (its write folder and config); the kit reads
and writes only through the recipe's callbacks.

## Original files (2026-11-26)

A game may name a catalog where its original files can be downloaded. No
catalog lists a source today, and none is fetched unless one does.

- **The recipe names it**: `Game::original_catalog` (an https:// address of
  JSON shaped as mw3-re's `catalog/games.json`) and `Game::original_key` (the
  game's entry there). Either one left empty means none is asked for, and the
  screen is as before. The kit names no catalog and no game.
- **The entry's `"original"`** is a list of sources, the same file at each
  address, tried in order: `{"url", "sha256", "size", "name", "note"}`, at
  most four (`rekit/original.h`, `read_original`). Each address must be
  https:// with a plain host. The sha256 is 64 hex digits, the size is in
  bytes, and the name is a plain file name. One bad source and the whole
  list is not used. file:/// is taken only in checks
  (`REKIT_SETUP_ALLOW_FILE`).
- **The find screen** asks the catalog on its own thread at the start, and
  the screen stays usable while it waits. Only when the entry lists sources
  does DOWNLOAD THE GAME FILES appear under FIND THE GAME, its second line
  being the first source's `note`. A catalog that doesn't answer, has no
  entry, or lists nothing leaves the screen as before, with one log line
  (`original: none offered: ...`).
- **Pressing it is the player's consent**: no further question.
  "GETTING THE ORIGINAL GAME FILES" shows the progress and a CANCEL. The
  system's curl fetches each source in turn (`rekit/sdl/setup_original.h`),
  HTTPS only (redirects too), into `<temp>/rekit-original-<id>/<name>`. That
  folder is made with the kit's mark, `rekit-original-download.txt`, and a
  folder of that name without the mark that holds anything is never written
  to. A file of the wrong size or SHA-256 is deleted and the next source
  tried. The first good one is looked at exactly as a pick of that file
  would be, then installed at once into the default place, with no "Use
  these game files?" (the press was the go; 2026-11-26). Another folder
  stays FIND THE GAME's or a pick's.
- **The file is deleted** once it is installed, refused, passed over (BACK,
  pick other files) or the screen closes. The folder goes with its mark once
  nothing else is in it.
- **The launcher** does the same for a game it installs, before any screen.
  It reads the catalog it already has, fetches with the same checks into a
  marked folder of its own, and hands the file to the game's served
  `--setup` (`--look`, then `--install`), as PICK A FILE does (mw3-re
  docs/launcher.md, Games on demand). A game run by the launcher therefore
  needs nothing beyond its served setup.

## Where the code comes from (mw3-re, 2026-11-21)

| Kit piece | From | Notes |
|---|---|---|
| archives, cooked sectors | `src/mech3/source_archive` | already game-free |
| cue sheets, CD audio | `src/platform/cue_audio` | already game-free |
| InstallShield 5 | `src/mech3/patch_import` (`read_installshield`, the in-place reader) | split from the 1.2 patch's recipe |
| ZIP, deflate | `src/gamez/zutil/zutl_zip` | game-free |
| staging, safe paths, space | `src/mech3/source_install` | split from MW3's layout |
| walking sources | `src/mech3/source_map` | the game test moves to recipes |
| disc reading | `src/mech3/disc_install` (`Disc`) | MW3's install steps become its recipe |
| install search | `src/frontend/sdl/sdl_install` | registry keys become recipe hints |
| pickers | `src/frontend/sdl/system_pickers` | game-free |
| helper programs | `src/frontend/sdl/sdl_child` | game-free |
| the user folder elsewhere | `src/mech3/data_folder` (2026-11-25: `rekit/data_folder.h`) | the game's name and settings files given |
| the program's record | `src/frontend/sdl/sdl_games` `record_self` (`rekit/program_record.h`) | the same file and lines |
| movie import runner | `src/frontend/sdl/movie_import`, `first_run_import` | a first-run step type |
| MW1's finder | `src/frontend/sdl/mw1_files`, mw1's `mw1_start` | becomes MW1's recipes |
| setup screen | the launcher's FIND THE GAME and GAME FILES | redrawn without the engine |

Recipes that stay in their game: MechWarrior 3's (data versions, the 1.2
patch, the disc's file groups, `Found::Game`), Pirate's Moon's, the MW1
remake's (three installs, the first-run conversions, the arctic worlds),
MechWarrior 1's (BTECH.EXE and its revisions).

## Order

1. This design; the repo; CMake target `rekit` (core) and `rekit_sdl`.
2. The game-free leaves, moved with their tests: archives, cue, ZIP,
   InstallShield, safe paths and staging, pickers, helper programs.
   mw3-re uses the kit as a git submodule (`extern/re-setup-kit`) on a
   branch, merged after its 1.2.2 release; behaviour unchanged, every
   existing setup test passing.
3. The recipe interface; MechWarrior 3's and Pirate's Moon's recipes in
   mech3-re / mech3-pm-re; `source_map` and the search split.
   Done so far: the walk (`rekit/sources.h`: `find_source` over a recipe's
   `Clue`s, each judged into an `Identity`) and the disc reader
   (`rekit/disc.h`); mw3-re's `map_source` is now MechWarrior 3's and Pirate's
   Moon's clues (zbd/reader.zbd, DATA1.HDR) over it; the install search
   (`rekit/search.h`) takes each game's registry keys and folder names as
   `SearchHints`. Step 3 is done.
4. `--status` and `--setup` in mw3 and mw3pm; the launcher switched to them.
5. The setup screen in the kit; each game opens it on its own, with its
   own theme (mech3-re's launcher art and font for MechWarrior 3 and
   Pirate's Moon; MW1's and Inception's their own). The screen's words are
   the kit's with the game's name filled in; nothing game-specific stays in
   the kit.
   Done so far (branch `setup-screen`): `rekit/sdl/setup_screen.h`,
   `run_setup(Game, Theme, SetupOptions)` returning the contract's
   `SetupExit`: FIND THE GAME (the search, then the picker), "Use these game
   files?" (INSTALL, CHANGE FOLDER, BACK, the find's plate to pick others)
   and the working screen (progress, CANCEL), on a worker thread; mouse,
   keys and gamepads. Drawn with SDL's renderer at 854x480, letterboxed; the
   text through stb_truetype at the window's pixels (sharp on HiDPI); a
   theme's PNG art (badge, background, bands, a nine-sliced plate) through
   stb_image; flat plates in the theme's colours without art. The game
   folder and its mark (`rekit/game_folder.h`: `game_folder_in`,
   `claim_game_folder`, `media_dir`) are mw3-re's, with the same files and
   `game=<key>` line. The log keeps mw3-re's lines (`screen <name>: ...`,
   `header <title>`, `work: <TITLE>`, `Nothing was copied: ...`); the
   checks drive it through `REKIT_SETUP_*` (the header lists them) and
   never open a picker or show a window.
6. MW1 remake's and MechWarrior 1's recipes; then Inception's.

Each step ships with its tests and leaves every game working.
