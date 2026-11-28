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
  How it draws over the game (its own window, or a layer the game's
  renderer draws) is decided per game by its hook. The kit offers both, and
  the game pauses while the menu has the focus, as it does when it loses the
  focus.
- **The hint:** for the first 5 seconds of a start, small text bottom-left:
  "Setup: Shift+Alt+F1" (the Mac's keys on a Mac). It shows on the first few
  starts (a count kept in the game's user folder), then no more.
- **A way in without keys:** each game gives the menu a button of its own
  (MW3 and PM: the Upgrades page).
- Started by the launcher, the game shows no hint and never asks about
  updates (the launcher does both). The keys still work.

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
  - UPDATE NOW asks to close the game, then installs as the launcher does:
    the signed checksums checked, the files swapped in outside the running
    program, the game restarted;
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
  their sums, and repaired where the kit can.
- **RESET SETTINGS:** back to the defaults; saves are never touched.
- **OPEN MY DATA FOLDER:** where saves, logs, music and mods live.

## 4. Updates when a game starts on its own

- At a start not from the launcher, at most once a day: one small request
  for the newest release (the catalog's homes, as the launcher reads them).
  Offline or slow, it says nothing and never delays the start.
- With a newer one: "<Game> <version> is available." with UPDATE NOW, NOT
  NOW and DON'T ASK AGAIN. DON'T ASK AGAIN is kept in the game's settings,
  and UPDATES in the menu still works.

## 5. Shared with the launcher

The catalog reader, its signature check, the release lists, downloads and
resume, the checksum checks, the swap and restart, and the update counts
move into the kit (or are reused from it), so the launcher and the games run
the same code. Nothing game-specific comes in (DESIGN.md, The rule).

## 6. Order

1. The launcher's WHAT'S NEW tab (the hub only; its own item).
2. The kit: the menu over a running game, the keys and the hint, the
   launcher look as the default theme, REPORT A PROBLEM (catalog field +
   fallback), OPEN MY DATA FOLDER.
3. The kit: UPDATES, the start-up check, the save backups, the counts.
4. CHECK MY FILES and RESET SETTINGS.
5. Each game takes the kit version and adds its hook (the menu's drawing and
   pause, its button, its paths). Its own release follows.

## Open

- Each game's renderer decides how the menu draws over it (window or layer).
  Decide per game in step 5.
- Whether a game's original keys already use these combinations. Each game
  checks its own bindings in step 5; the combination can be changed per game
  in its recipe if one clashes.
