#pragma once
// A game updating itself (docs/plan-in-game-menu.md sections 3 and 4, UPDATES; 2026-11-27): the logic, with no
// network and no SDL (rekit/sdl/update.h fetches and installs). The same shapes as mw3-re's launcher reads: the signed
// catalog's "homes" (where every repo's releases list is asked) and "counts", a releases list in GitHub's JSON
// (Forgejo answers the same), a release's SHA256SUMS and its signature. How the game was installed decides how an
// update is swapped in; the game's hook reports it. The kit names no game: the recipe gives repo, tags and files.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
const char* install_kind_name(InstallKind k);  // "windows-setup-user", for the log

// Where a game's releases are and which file updates it (the game's recipe).
struct UpdateRecipe {
  std::string key;         // its catalog entry ("mw3"), the counts' game=
  std::string repo;        // its releases list's {repo} ("mech3-re")
  std::string tag_prefix;  // its tags are this and X.Y.Z ("v", "pm-v", "mw1r-v")
  std::string tag;         // this program's tag ("v1.7.0"; a build between releases, "v1.7.0-3-gabc", counts as v1.7.0)
  std::string file;        // the release's file this install updates from, "{tag}" for the tag ("MW3-alone-{tag}-mac.dmg")
};

// A catalog's update fields: "homes" ([{"key", "releases", "order"}], "releases" an https address with {repo} and
// optionally {count}) and "counts" (an https address; "" none). The catalog is checked as signed before (sign.h).
struct ReleaseHome {
  std::string key, releases;
  int order = 0;
};
struct UpdateCatalog {
  std::vector<ReleaseHome> homes;  // by order
  std::string counts;
};
constexpr int kMaxReleaseHomes = 4;
// False with `why` when it is not a catalog or lists no usable home (a bad home is left out, the rest kept).
// `allow_file`: file:/// addresses too (checks).
bool read_update_catalog(const std::string& catalog_json, bool allow_file, UpdateCatalog* out, std::string* why);
// A home's releases list for `repo`: {repo} and {count} filled in.
std::string releases_address(const ReleaseHome& home, const std::string& repo, int count);

// A releases list (an array, GitHub's JSON): each release's tag, notes, page and files.
struct ReleaseFile {
  std::string name, url;  // assets[].name, assets[].browser_download_url
  uint64_t size = 0;
};
struct Release {
  std::string tag, notes, page;  // tag_name, body (Markdown), html_url
  bool draft = false, prerelease = false;
  std::vector<ReleaseFile> files;
  const ReleaseFile* file(const std::string& name) const;
};
bool parse_releases(const std::string& json, std::vector<Release>* out, std::string* why);
// `prefix` then X.Y.Z and nothing after ("pm-v1.2.3"; not "pm-v1.2.3-rc1" or "v1.2.3" for "pm-v").
bool release_tag(const std::string& prefix, const std::string& tag);
// `pattern` with each "{tag}" as `tag`.
std::string release_file_name(const std::string& pattern, const std::string& tag);

// One home's answer: its list's JSON, or why there is none.
struct HomeAnswer {
  std::string home;  // its key
  std::string json, error;
};
// The check's verdict over the homes' answers (in the homes' order, the first asked first for a download).
struct UpdateCheck {
  enum Kind { kFailed, kUpToDate, kNewer } kind = kFailed;
  std::string tag, page;  // the newest release's (kNewer, kUpToDate)
  // The newest release as each home has it with the file and a signed sum list: its file, SHA256SUMS and
  // SHA256SUMS.sig, in download order. Empty with kNewer: no home can install it (signed only; the page instead).
  struct Source {
    std::string home;  // the home's key ("gh" or "github" for GitHub, else a mirror; see github_home)
    ReleaseFile file, sums, sig;
  };
  std::vector<Source> sources;
  // Every release newer than this program's, newest first: its tag and notes (the UPDATES page's "every version
  // between").
  std::vector<std::pair<std::string, std::string>> notes;
  std::string words;  // for the log and the page
};
// A release counts when it is not a draft or a prerelease, its tag is the recipe's, and it has the recipe's file.
// Failed only when no answer reads (the first one's reason) or this program's own version is unknown.
UpdateCheck judge_update(const UpdateRecipe& recipe, const std::vector<HomeAnswer>& answers);

// The SHA-256 SHA256SUMS gives `name` ("<64 hex>  <name>" or "<hex> *<name>"), lower case; "" when none.
std::string sums_line(const std::string& sums, const std::string& name);

// `fresh` (a file, or a folder such as a Mac app or an unpacked game) put in `target`'s place, in `target`'s folder
// only: copied beside it as <target>.new, the old moved aside as <target>.old, the new renamed in, the old removed
// last (a Mac app keeps its folder and exchanges its Contents, so the Dock keeps it). Any failure puts the old back
// and removes what was copied, so the program is never half replaced. False with `error`. Nothing outside `target`
// and those two siblings is touched; a leftover .new or .old from an earlier try is removed only when it is a file or
// holds `marker` (a path inside, "Contents/MacOS/mw3", "mw3.exe"), else it is the player's and the swap stops.
bool swap_in(const std::string& fresh, const std::string& target, const std::string& marker, std::string* error);

// The player's files are backed up before an update changes anything with rekit/backup.h (back_up).

// The player's update choices and the last start-up check, in <user_folder>/rekit-updates.txt.
struct UpdatePrefs {
  bool ask_at_start = true;  // "Check for updates when <game> starts" (DON'T ASK AGAIN unticks it)
  int64_t last_check = 0;    // seconds since 1970, the last start-up check's
};
constexpr const char* kUpdatePrefsName = "rekit-updates.txt";
UpdatePrefs read_update_prefs(const std::string& user_folder);
bool write_update_prefs(const std::string& user_folder, const UpdatePrefs& prefs);
// A start-up check is due: asked for, and none in the last day (a clock set back counts as due).
bool start_check_due(const UpdatePrefs& prefs, int64_t now);

// The count of a finished update (mw3-re docs/launcher.md, Update counts), as the launcher sends it: `base` the
// catalog's "counts"; kind=update; the versions as players read them; src "github" or "mirror" (where the file came
// from); `agent` the launcher= value (the launcher sends its version; a game sends "game").
// A catalog home that is GitHub: keyed "gh" (the live catalog) or "github" (2026-11-28: the kit looked only for
// "github", so every real update counted as the mirror's). The count's src and the update log follow it.
bool github_home(const std::string& key);
std::string update_count_url(const std::string& base, const std::string& game, const std::string& from, const std::string& to,
                             bool github, const std::string& agent);

// A release's archive (kFolder: a zip or a tarball) unpacked into `to` (made here, refused when it exists), its one
// top folder taken off when everything sits in one. Safe paths only (files.h). False with `error`; nothing left then.
bool unpack_release(const std::string& archive, const std::string& to, std::string* error);

// Windows: the update finished outside the running game by a hidden cmd script, written by the kit and run once the
// game has quit (its process `pid` gone): the new Setup run silently in the install's scope (Inno Setup's
// /VERYSILENT, /CURRENTUSER or /ALLUSERS; same AppId, so an upgrade in place) and waited for, or (a folder install)
// the unpacked `fresh` moved into `target`'s place (the old moved aside first, put back on a failure, removed last);
// then `restart` started and the script and the download removed. Paths are UTF-8, and the script (written as UTF-8,
// no BOM) sets cmd's code page to 65001 first. "" when a path holds a character the script can't carry (% or ").
std::string windows_setup_script(const std::string& setup, bool all_users, unsigned long pid, const std::string& restart);
std::string windows_folder_script(const std::string& fresh, const std::string& target, unsigned long pid, const std::string& restart);

// Elsewhere: /bin/sh's arguments that wait for process `pid` to end (at most two minutes), then start `restart` (a
// program and its arguments, each passed as one word, never read by the shell).
std::vector<std::string> posix_restart_command(unsigned long pid, const std::vector<std::string>& restart);

// The finished update's note (plan section 3, the counts): left in the user folder before the game quits for it, and
// taken (read and removed, so never twice) by the next start, which reports it when its own tag is `to`. `counts` is
// the signed catalog's "counts" as the check read it (taken back only when it is an https:// address; else "").
constexpr const char* kUpdateNoteName = "rekit-updated.txt";
struct UpdateNote {
  std::string from, to, counts;
  bool github = false;
};
bool write_update_note(const std::string& user_folder, const UpdateNote& note);
bool take_update_note(const std::string& user_folder, UpdateNote* note);

}  // namespace rekit
