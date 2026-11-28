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
recipe ("Find MechWarrior 1's files", "Copied into ..."). What a theme
leaves out is the launcher's look, the kit's default (2026-11-27,
`rekit/sdl/theme.h`): dark slate panels with #3c4552 edges, the amber
accent #f2a51f, light words on #20262f buttons, Saira Condensed Bold and
Medium (OFL) and the launcher's click and hover sounds, built into
`rekit_sdl` (`assets/`, THIRD-PARTY). The same Theme draws the menu over a
running game (docs/plan-in-game-menu.md).

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

A game may name a catalog where its original files can be downloaded.
Nothing is fetched unless the catalog lists a source and the player asks.

- **The first start does it alone (2026-11-27, docs/plan-in-game-menu.md
  section 1).** A fresh setup (no files in use) searches this computer
  first. A copy found is used at once: looked at and installed in a small
  window (what is coming, its size, the progress, CANCEL and USE MY OWN
  COPY INSTEAD), with no question. A copy with something to say (its files
  missing or damaged, an unknown edition: the recipe's `asks_first`, else
  a find with no version) stops on the confirm screen in the setup window
  instead, with the game's words for it: it may still be usable. None found, the catalog's download
  starts at once in the same small window, then installs. A failure, a
  cancel, the link or no source opens the setup window (an ordinary
  window, never fullscreen, so the system's pickers behave) on GAME FILES:
  FOUND ON THIS COMPUTER (USE THIS), USE MY OWN COPY (PICK A FILE, PICK A
  FOLDER, SEARCH AGAIN) and DOWNLOAD THEM. A game that already has its
  files never opens the setup at all.
- **A test run has no network (2026-11-27).** `RE_KIT_TEST=1` (a game's
  test runs set it): no catalog is asked and `fetch_url` refuses any
  address but file:///, before curl.
- **Two lists.** An entry's `"original"` sources come first, in order (the
  original download, such as a disc image), then its `"compact"` ones: a
  smaller repack of the same files that only newer games read. A compact
  source may give `"game_min"` (a dotted version); it is skipped unless
  `Game::version` (the program's version or release tag, `pm-v1.2.12`
  read as 1.2.12) is known and not lower.

- **The recipe names it**: `Game::original_catalogs` (the https://
  addresses of one catalog, JSON shaped as mw3-re's `catalog/games.json`,
  asked in order), `Game::catalog_keys` (the public keys its signature must
  check against, Signed data below) and `Game::original_key` (the game's
  entry there). Any of them left empty means none is asked for, and the
  screen is as before. The kit names no catalog, key or game.
- **Only a signed catalog is read.** Each address's `<address>.sig` is
  fetched with it. The first catalog whose signature checks against one of
  the recipe's keys is used. An unsigned or wrongly signed copy is passed
  over and the next address asked, with each reason kept for the log line.
- **The entry's `"original"`** is a list of sources, the same file at each
  address, tried in order: `{"url", "sha256", "size", "name", "note"}`, at
  most four (`rekit/original.h`, `read_original`). Each address must be
  https:// with a plain host. The sha256 is 64 hex digits, the size is in
  bytes, and the name is a plain file name. One bad source and the whole
  list is not used. file:/// is taken only in checks
  (`REKIT_SETUP_ALLOW_FILE`).
- **The setup** asks the catalog on its own thread at the start, and the
  setup window stays usable while it waits. Only when the entry lists
  sources does DOWNLOAD THEM offer DOWNLOAD, with the first source's `note`
  and size. A catalog that doesn't answer, has no entry, or lists nothing
  says no download is available, with one log line (`original: none
  offered: ...`).
- **Starting it is the player's consent** (the first start's, or
  DOWNLOAD): no further question.
  "GETTING THE ORIGINAL GAME FILES" shows the progress and a CANCEL. The
  system's curl fetches each source in turn (`rekit/sdl/setup_original.h`),
  HTTPS only (redirects too), into `<temp>/rekit-original-<id>/<name>`. That
  folder is made with the kit's mark, `rekit-original-download.txt`, and a
  folder of that name without the mark that holds anything is never written
  to. A file of the wrong size or SHA-256 is deleted and the next source
  tried. The first good one is looked at exactly as a pick of that file
  would be, then installed at once into the default place, with no "Use
  these game files?" (the press was the go; 2026-11-26). Another folder
  stays a pick's (then CHANGE FOLDER).
- **The file is deleted** once it is installed, refused, passed over (BACK,
  pick other files) or the screen closes. The folder goes with its mark once
  nothing else is in it.
- **The launcher** does the same for a game it installs, before any screen.
  It reads the catalog it already has, fetches with the same checks into a
  marked folder of its own, and hands the file to the game's served
  `--setup` (`--look`, then `--install`), as PICK A FILE does (mw3-re
  docs/launcher.md, Games on demand). A game run by the launcher therefore
  needs nothing beyond its served setup.

## Signed data (2026-11-26)

`rekit/sign.h` checks Ed25519 signatures (RFC 8032) with Monocypher 4.0.2
(`third_party/monocypher`, CC0 or BSD 2-clause, THIRD-PARTY.md). It is the
family's one copy of that code: mw3-re's launcher checks its catalog and
each release's SHA256SUMS with it too.

- **A signed file** is published with a detached `<file>.sig` beside it.
  The `.sig` holds one or more lines of 128 hex digits, each a signature of
  the whole file's bytes by one key. Keys are 64 hex digits.
- **Several lines are for moving keys.** While a new key comes in, the file
  carries the old key's signature and the new one's. Programs that know
  either key take it, and the old line is dropped once every copy knows the
  new key. A game's recipe lists its keys. mw3-re's launcher also learns a
  catalog's `next_keys` (docs/launcher.md there); the kit has no cache to
  remember them in, so a game relies on the lines.
- **The kit never signs.** The private key is kept outside every repo.
  mw3-re's `tools/package/sign.py` signs, with the key's path in an
  environment variable. The checks sign with test keys only
  (`tests/test_keys.h`, seeded from one byte).
- **Fresh copies.** A catalog and its `.sig` are asked with
  `Cache-Control: no-cache` and `Pragma: no-cache` (`fetch_url`'s `fresh`),
  so a proxy between serves no stale copy: a site may mark its static files
  good for years. A game's files are downloaded without them (their SHA-256
  is in the signed catalog).

## The menu inside a game (2026-11-27)

`rekit/sdl/game_menu.h` is the setup menu over a running game
(docs/plan-in-game-menu.md, step 2b). The game makes one `GameMenu` at its
start from a `MenuHooks` it fills in and its `Theme` (theme.h), and calls
it in two places in its loop:

- **`event(e)` for every SDL event, first.** The game's own events reach it
  through `hooks.deliver`, at once and in order. F1 of the keys
  (Shift+Alt+F1, Cmd+Shift+F1 on the Mac) never reaches the game, down or
  up. Shift and Alt (Cmd) do reach it at once, as every key does, because
  a game may bind them alone (sprint, a held fire mode) and a delay would
  show in play. As the menu opens, everything the game holds down (those
  modifiers, a movement key, a mouse or pad button) gets its up sent to
  the game, and the real ups and key repeats that follow are the kit's.
  Whatever goes down in the menu stays the kit's after it closes. Held
  keys never stick on either side. Esc closes the menu. While it is open,
  the menu keeps all input; window, device and quit events still reach
  the game.
- **`frame()` after the game's frame, before it is shown.** When the menu
  or the hint shows, `hooks.draw` gets the picture. That is one RGBA image
  (straight alpha) at the window's pixel size, drawn over the whole window.
  Its `serial` changes only when the picture does. The kit draws it on a
  software renderer of its own (`SDL_CreateSoftwareRenderer`), so every
  game draws it the same way, whatever it renders with: SDL_Renderer, SDL
  GPU or GL.
- **The clock.** `hooks.pause` runs when the menu opens and `hooks.resume`
  when it closes. Each game defines its own pause. A game that runs on
  under the menu even so (a network game) says so with `hooks.runs_on`,
  and the header reads THE GAME IS NOT PAUSED while it does.
- **The pages:**
  - GAME FILES: the copy in use, and CHANGE (RESTARTS THE GAME). That asks
    first, then calls `hooks.restart_for_setup`; the game closes and
    `relaunch_for_setup({"--setup"})` starts it again with its windowed
    setup. No picker opens over a running game.
  - UPDATES (step 3; see Updates below): the newest version and every
    newer version's notes, a "Check for updates when <game> starts"
    checkbox, CHECK NOW, the backup line and UPDATE NOW, which asks to
    close the game first. The section shows a count of newer versions.
  - REPORT A PROBLEM: the log's folder, with OPEN THE LOG FOLDER and COPY
    THE PATH, and OPEN THE REPORT PAGE. That page is `hooks.report_url`,
    replaced by the catalog's "report" for the game when one is read
    (`catalog_report`, `set_report_url`; https only).
  - CHECK MY FILES (step 4): the program's folder (`hooks.program_dir`)
    and the game files (`hooks.files_in_use`), each against its record,
    `rebuild-files.sha256` at its top (`rekit/manifest.h`, sha256sum's
    format). The kit writes the game files' record when it installs them
    (`install()` in setup_ops.cpp, the last stage "Recording the files"),
    the one install both the setup window (its first start included) and
    `--serve` go through.
    A game's packaging writes the program's with `tools/files_manifest.py`
    on the finished folder: on the Mac before codesign, on the app's
    `Contents/Resources` (the game's `program_dir` points there; codesign
    refuses a plain file at the top of `Contents`, and the signature seals
    `Info.plist` and the Mach-Os itself; the record is inside the signed
    .app, so writing it afterwards breaks the seal; any Mach-O in Resources
    is left out, as codesign rewrites each), on Linux before the AppImage is squashed, and on Windows before
    the zip and the Setup (`tools/files_manifest.ps1` where the package step
    has no Python: the same bytes). A folder-install update swaps the whole
    folder, so the new record comes with it. The check runs on a thread with a
    progress bar and CANCEL, and closing the menu stops it. Files added
    later (mods, saves) are not looked at, and nor are the files the
    recipe leaves out (`hooks.leave_out`). The page lists up to six
    changed or missing files and logs them all. It says when there is
    no record (a development build, an install from before records).
    Game files with no record go to the game's own check instead
    (`hooks.check_game_files`, the problems in its words). When it finds
    none, the kit writes their record, so later checks use it. A program
    folder with no record only says so. Files the game rewrites in its own
    folder (an original's config file, a high-score table) are listed in
    `hooks.rewritten`: in the record, never compared.
    It repairs nothing. It points to GAME FILES' CHANGE for the game
    files and to installing again over the copy for the program.
  - RESET SETTINGS (step 4): asks first, then copies `hooks.settings`
    (files or folders) to `Backups/<date time> settings` in the user
    folder (`rekit/backup.h`, the newest five kept), then calls
    `hooks.reset_settings`. The game resets them, in memory and on disk,
    because only the running game can. A copy that fails resets nothing.
    The page says where the copy is. Without the hook, the page says the
    game resets its settings in its own options.
  - OPEN MY DATA FOLDER: the user folder, with OPEN and COPY THE PATH.
- **The game's own entries** (`hooks.entries`, each a label and a
  callback) come after the sections. Picking one closes the menu and
  resumes the game, then calls the callback. The callback runs after the
  game has resumed, so a game whose page must open over a paused battle
  pauses again itself. An entry's `available` (null: always) is asked as
  the menu draws. While it says false, the entry is dimmed with its
  `unavailable_note` as a sub-line: the keys and the mouse still light it,
  so the note can be read, but a click or Enter does not pick it (the log
  says why). Revenge's UPGRADES opens only at certain points; MechWarrior
  3's is not reachable in a debug mission run.
- **The hint** is "SETUP Shift + Alt + F1", bottom left, for the first 10
  seconds of each of the first three starts. The count is kept in
  `setup-hint.txt` in the user folder. There is no hint when `RE_LAUNCHER`
  is set. `RE_KIT_TEST=1` turns off both the hint and the keys, though a
  game's own button (`show()`) still opens the menu. The hint fades in over
  0.4 s and out over the last 0.8 s, by the clock (the game's frame rate
  does not change it); its `serial` changes only while it fades. A small ×
  at its top right closes it for this start: it fades out at once, and the
  count of starts is as it was. Only a click inside the ×'s own 26 px
  square is the kit's (its up too). Every other click, on the rest of the
  hint included, is the game's, and none is taken while the game holds the
  mouse (relative mode).
- **Sounds** play nothing under `RE_KIT_TEST`, on the setup screen and in
  the menu, so a test run's presses are silent. The kit's test runner asks
  SDL for its dummy audio driver as well (unless one is set), for every
  test and every program a test starts.
- **Keys in the menu:** Up and Down move through a column. Right goes into
  the page's buttons, and Left goes back to the sections. Enter (a pad's A)
  presses. Esc (a pad's Start) closes. A pad's B goes back, then closes.
  The mouse lights what it is over and clicks it.
- **Checks:** tests/test_game_menu.cpp, in a test host, with
  `REKIT_MENU_SHOTS=<dir>` saving each page's picture.

## Updates (2026-11-27)

`rekit/update.h` is the logic, with no network and no SDL;
`rekit/sdl/update.h` fetches, installs and restarts. A game fills in
`hooks.update` (an `UpdateSource`): its recipe (repo, tag prefix, this
program's tag, the release file it updates from), the signed catalog's
addresses and keys, the paths in the user folder that are backed up, and
for a folder install the path that marks the folder as the game's.

- **The check.** The signed catalog's `homes` (as the launcher reads
  them) are asked in order for the repo's releases. A release counts
  when it is not a draft or a prerelease, its tag is the game's, and it
  has the game's file. Every fetch is capped at 20 s. With
  `RE_KIT_TEST=1` nothing goes over the network.
- **Signed only.** An update installs only from a release whose
  SHA256SUMS has a `.sig` that checks against the game's keys, and whose
  file matches its line there. There is no unsigned fallback; without a
  signature, the page offers the release page.
- **The start-up check** (`StartUpdateCheck`, on its own thread) runs at
  most once a day. It does not run when the launcher started the game
  (`RE_LAUNCHER`), when the player unticked the checkbox, or in a test
  run. Offline it says nothing and does not delay the start. With a newer
  release it shows the prompt: UPDATE NOW (to the page, asking to close),
  NOT NOW, and DON'T ASK AGAIN (which unticks the checkbox). The choices
  live in `rekit-updates.txt` in the user folder.
- **UPDATE NOW**, once the player agrees to close the game: download (a
  dropped connection resumes), check the signed sums, back up into
  `Backups/<date> update` (the newest five kept, `back_up`), leave the
  update note, then put the update in by install kind:
  - Windows Setup, per-user or per-machine: a hidden helper script waits
    for the game to quit, runs the new Setup silently in the same scope
    (`/CURRENTUSER` or `/ALLUSERS`), and starts the game again.
  - A Windows folder: the same helper moves the unpacked folder in.
  - A folder elsewhere, an AppImage, a Mac app (from the release's disk
    image, swapping its Contents): `swap_in` now. A helper starts the
    game again once this process has gone.
  - Nothing changes for an unknown or development install, a copy the
    launcher installed (the launcher updates it), or a Mac app run
    translocated (the page says to move it to Applications).

  The game then quits through `hooks.quit` (null pushes
  `SDL_EVENT_QUIT`).
- **The count.** The next start takes the update note and, when it is
  the version the update installed, sends the catalog's `counts` address
  with `kind=update` and `launcher=game`, as the launcher sends its own.
- **An update that did not finish.** When the next start is still the
  older version (a declined Windows prompt), nothing is counted, the
  start-up check runs whatever the day and the checkbox say, and the
  prompt says so once: UPDATE NOW, RELEASE PAGE, NOT NOW (only OK when
  offline). The note is taken either way, so the next start says nothing.
- **Checks:** tests/test_update.cpp (the logic), test_update_sdl.cpp and
  test_update_menu.cpp against `file:///` stand-ins signed with test keys
  (tests/update_world.h), with `REKIT_MENU_SHOTS=<dir>` saving the prompt
  and the page.

## Settings (2026-11-28)

The kit's tunable values live in data, not in the code (`rekit/settings.h`, `assets/setup-kit-settings.json`):
the hint's starts, time and fades, CHECK MY FILES' list length, the menu's volumes, REPORT A PROBLEM's page, where
the signed catalog is asked, how often the update check and the live values run, and the network time limits.

Layers, each over the one before:

1. the kit's defaults (equal to its behavior before the file, so a missing file changes nothing);
2. the shipped file, `setup-kit-settings.json` beside the game's program (`program_dir`: Contents/Resources on the
   Mac). Each game ships its own copy, with its own `report_url` and `catalogs`, and lists it in its record;
3. the live values from the signed catalog: its top-level `setup_kit` (every game), then the game's entry's
   `report` and `setup_kit`. The menu asks the catalog on its own thread at most every `live_every_hours` and keeps
   what it read in `<user_folder>/setup-kit-live.json`, so a change reaches players without a release, offline too;
4. the player's own copy of the file in the data folder.

Each value is clamped to its range; a wrong type, or an address that is not https://, keeps the value before, and
the log says which. Never in the file: the signing keys, https-only, checksum and signature checks. The catalog's
values are trusted only because its signature checks against the keys the game was built with.

When the kit contacts the network on its own: the start-up update check (at most every `update_check_hours`, only
when the player left "Check for updates when <game> starts" ticked, never under the launcher), and the live values
above (at most every `live_every_hours`, under the same two conditions, or when the player opens the menu). Nothing
in a test run (RE_KIT_TEST). Everything else (CHECK NOW, UPDATE NOW, the original files' download) is the player's
press.

The update source's `keep_now` (2026-11-28) is asked at backup time, for saves made after the start.

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
   Since 2026-11-27 (step 2a of docs/plan-in-game-menu.md): the first
   start's small window (600x270) and the setup window (960x540,
   resizable, GAME FILES), the launcher's look as the default theme, two
   fonts (bold and body), and `REKIT_SETUP_PREVIEW` for review pictures.
   FIND THE GAME's old way stays behind `REKIT_SETUP_FIND`.
6. MW1 remake's and MechWarrior 1's recipes; then Inception's.

Each step ships with its tests and leaves every game working.
