# re-setup-kit

The shared "find your game files" kit used by the -re game rebuilds and
remakes (mech3-re, mech3-pm-re, mech1-remake-re, mech1-re, inception-re and
the launcher at mw3-re). It lets a game find or take the player's own copy of
the original game, from an installed folder, the disc, a disc image or an
archive, on Mac, Windows and Linux, without running the original installer.

No game's files are included or downloaded: every game still needs your own
copy of the original.

Used by the BattleTech family's games:
[mech3-re](https://github.com/telltaleatheist/mech3-re),
[mech3-pm-re](https://github.com/telltaleatheist/mech3-pm-re),
[mech1-remake-re](https://github.com/telltaleatheist/mech1-remake-re),
[mech1-re](https://github.com/telltaleatheist/mech1-re) and
[inception-re](https://github.com/telltaleatheist/inception-re). How it is
built: [DESIGN.md](DESIGN.md).

MIT licensed. The third-party code it carries, and the notices a game
ships with it: [THIRD-PARTY.md](THIRD-PARTY.md).

## Mac signing

`tools/macos/sign_notarize.sh` signs a game's (or the launcher's) Mac app and
DMG with a Developer ID, and notarizes and staples the DMG, so the game opens
like any other app. A game's `make_app.sh` calls it in three steps:

```sh
MODE=$(extern/re-setup-kit/tools/macos/sign_notarize.sh check)   # "notarize" or "unsigned"; refuses otherwise
extern/re-setup-kit/tools/macos/sign_notarize.sh app "$APP" --entitlements my.entitlements
# ... the DMG built from the signed app ...
extern/re-setup-kit/tools/macos/sign_notarize.sh dmg "$DMG" "$APP"
```

The identity and the notary profile come from the environment
(`RE_SIGN_IDENTITY`, `RE_NOTARY_PROFILE`; the older `MW3_` names are read
too), so no name or team ID is ever in a repo. A build that would not be
notarized is refused unless `RE_ALLOW_UNSIGNED=1` is set, for development
and check builds. `check` says which kind of build this is, so a game can
pick its read-me's first-open paragraph by it. The script's header lists
every option.

Gatekeeper checks only what carries the quarantine flag (a browser's
download). A game the launcher downloads and installs itself has none, so a
signed launcher matters first; a game's own DMG matters when a player
downloads it directly.
