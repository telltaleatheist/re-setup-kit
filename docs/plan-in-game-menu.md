# Plan: the kit inside the games (2026-11-27)

Agreed 2026-11-27. The kit grows from "find your game files" into the
player's menu for everything around a game: its files, its updates, its
problems. Every game gets it by taking a new kit version. The rule in
DESIGN.md still holds: the kit names no game, and each game describes itself
through its recipe and theme.

## 1. The setup menu, open at any time

- **The keys:** Shift+Alt+F1 on Windows and Linux, Cmd+Shift+F1 on the Mac.
  The kit takes them before the game sees them, at any moment: menus,
  missions, movies.
- **The game keeps running.** The menu opens over it and closes back to it.
  Nothing in the menu closes the game except an update, which asks first.
  It draws as a **layer on the game's own renderer**, after the game's
  frame and before it is shown, through the game's hook. Not a second
  window: one fights fullscreen, the focus and the Mac's Spaces, and
  hidden or background test runs.
- **Pause stops the game's clock,** not just its input. The hook freezes the
  game's time base (fixed-step ticks, timed narration, per-frame polls) so
  closing the menu causes no catch-up burst. Each game's hook defines its
  own pause; not every game pauses when it loses the focus today.
- **The keys are swallowed whole:** the modifiers and F1, key-down and
  key-up, never reach the game (games forward F1 and track held keys).
  Windows' Alt+Shift input-language switch is checked so it doesn't fire.
- **The hint:** for the first 5 seconds of a start, small text bottom-left:
  "Setup: Shift+Alt+F1" (the Mac's keys on a Mac). It shows on the first few
  starts (a count kept in the game's user folder), then no more.
- **No game files yet:** the game can't start, so the same menu opens as
  an ordinary window (never fullscreen), on GAME FILES, in the same look,
  so the system's file pickers behave. Once the files are in, the game
  starts as usual (fullscreen if set).
- **No picker over a running game:** a system file picker over a fullscreen
  game is unreliable (macOS Game Mode slowed the launcher's). Changing the
  game files needs a restart anyway, so in a running game GAME FILES shows
  the copy in use and CHANGE (RESTARTS THE GAME), which asks, closes the
  game and opens the windowed setup.
- **A way in without keys:** each game gives the menu a button of its own
  (MW3 and PM: the Upgrades page).
- **Started by the launcher:** the launcher sets the environment variable
  `RE_LAUNCHER` to its version when it starts any game, and every game
  checks that one name. Then the game shows no hint and never asks about
  updates (the launcher does both). The keys still work.
- **Test mode:** the environment variable `RE_KIT_TEST=1` (and each game's
  harness flag mapped to it) turns off the hint, the start-up update check
  and the menu keys, and the kit makes no network request at all, so
  screenshot goldens, trace suites and recorded runs are unchanged.

## 2. The look

The menu takes the launcher's look for every game: its fonts, colours, panel
and button shapes, sounds. That becomes the kit's shared default theme. A
game's theme still sets its title, art and wording. One look across the
launcher and every game.

## 3. What's in the menu

- **GAME FILES:** today's setup screen (find, pick, download the originals).
- **UPDATES:**
  - the installed version, and the newest with its notes (every version
    between, newest first);
  - **an update installs only when SHA256SUMS.sig verifies** against the
    catalog's key. There is no GitHub-only fallback here (it replaces
    programs). No signature means no in-game update: the menu points to
    the release page instead;
  - UPDATE NOW asks to close the game, then installs outside the running
    program and restarts it. **How it swaps depends on how the game was
    installed,** which the hook reports:
    - Windows Setup (per-user or per-machine): the new Setup.exe run
      silently in the same scope;
    - a zip or tarball: the folder swapped;
    - an AppImage: $APPIMAGE replaced;
    - Mac: the whole .app replaced from the notarized disk image, its
      signature kept. An app run from Downloads or the disk image
      (translocated) can't update itself: the menu says so and points to
      Applications;
  - before installing, the player's saves, pilots and settings are copied
    to a dated backup in the user folder (the last few kept), and the menu
    says where;
  - the update counts (the catalog's "counts") are sent as the launcher
    sends them.
- **REPORT A PROBLEM:**
  - opens the folder holding the logs (and shows its path), and opens the
    page where problems are reported;
  - that address is data, not code: a field in the signed catalog for each
    game (today the game's GitHub issues; later perhaps reverse-engineered.com
    or the git mirror), with the game's built-in copy as the fallback.
- **CHECK MY FILES:** the installed program and game files checked against
  their sums, respecting the recipe's known sums and its LeaveOut (a file
  left out on purpose is not damage). Repair re-copies from the recorded
  source or the catalog download.
- **RESET SETTINGS:** back to the defaults; saves are never touched.
- **OPEN MY DATA FOLDER:** where saves, logs, music and mods live.
- **Every path comes from the game's hook** and honors the game's own
  overrides (e.g. MECHWARRIOR_SAVE_DIRECTORY, CHR_DATA_DIR).

## 4. Updates when a game starts on its own

- At a start not from the launcher, at most once a day: one small request
  for the newest release (the catalog's homes, as the launcher reads them).
  Offline or slow, it says nothing and never delays the start.
- With a newer one: "<Game> <version> is available." with UPDATE NOW, NOT
  NOW and DON'T ASK AGAIN. DON'T ASK AGAIN unticks a checkbox on the
  UPDATES page, "Check for updates when <game> starts", so a player who
  changes their mind ticks it again; CHECK NOW sits beside it.

## 5. Shared with the launcher

The catalog reader, its signature check, the release lists, downloads and
resume, the checksum checks, the swap and restart, and the update counts
move into the kit (or are reused from it), so the launcher and the games run
the same code. Nothing game-specific comes in (DESIGN.md, The rule).

Mockup of the look (2026-11-27): the hint, UPDATES with its checkbox,
REPORT A PROBLEM, the start-up prompt, and the no-files window.

## 6. Order

1. The launcher's WHAT'S NEW tab (the hub only; its own item).
2. The kit: the menu over a running game, the keys and the hint, the
   launcher look as the default theme, REPORT A PROBLEM (catalog field +
   fallback), OPEN MY DATA FOLDER.
3. The kit: UPDATES, the start-up check, the save backups, the counts.
4. CHECK MY FILES and RESET SETTINGS.
5. Each game takes the kit version and adds its hook (the layer, the pause,
   its button, its paths, its install type). Its own release follows. The
   MW1 family goes mw1r, then chi, then chr (after chr's 1.0.0).

**Release tooling:** a release with no SHA256SUMS.sig fails its build
(sums.py without the signing key stops instead of warning), so no release
goes out unsigned.

## Open

- Whether a game's original keys already use these combinations. Each game
  checks its own bindings in step 5; the combination can be changed per game
  in its recipe if one clashes.
