#pragma once
// A game updating itself (docs/plan-in-game-menu.md section 3, UPDATES; 2026-11-27): the logic, with no network and
// no SDL. How the game was installed decides how an update is swapped in; the game's hook reports it.
namespace rekit {

enum class InstallKind {
  kUnknown,              // not known: no update installs (the menu points to the release page)
  kDevelopment,          // a build in its tree: never updated
  kWindowsSetupUser,     // Windows Setup for this user: the new Setup run silently, for this user
  kWindowsSetupMachine,  // Windows Setup for every user: the same, for every user (Windows asks to allow it)
  kFolder,               // a zip or tarball unpacked: the folder swapped
  kAppImage,             // Linux AppImage: $APPIMAGE replaced
  kMacApp,               // a Mac .app: replaced from the release's disk image (not while translocated)
};

}  // namespace rekit
