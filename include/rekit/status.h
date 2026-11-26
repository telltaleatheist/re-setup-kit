#pragma once
#include <string>
#include <vector>

// The launcher contract (DESIGN.md): `game --status` prints one JSON object
// and exits 0, nothing on screen; `game --setup` opens the setup screen and
// exits kSetupReady, kSetupCancelled or kSetupFailed.
namespace rekit {

constexpr int kStatusVersion = 1;  // "kit" in the JSON: bumped only when a field changes meaning
enum SetupExit { kSetupReady = 0, kSetupCancelled = 1, kSetupFailed = 2 };

struct Status {
  std::string game;     // the recipe's id: "mech3", "mech3-pm", ...
  std::string version;  // the program's ("1.2.2")
  enum State { kMissing, kReady, kIncomplete } state = kMissing;
  std::string where;    // the game's files' folder, when found
  std::string data;     // their version ("1.2"), "" unknown
  std::vector<std::string> missing;  // kIncomplete: what is not there, in plain words
  std::vector<std::string> pending;  // first-run steps still to do ("movies")
  std::vector<std::string> needs;    // other games' ids it needs
  std::string note;     // one line for the player, may be ""
  int setup = 0;        // kServedSetup when the program answers `--setup --search` and the rest (rekit/sdl/setup_serve.h)
  // Optional, each left out when empty (2026-11-25, for a launcher that starts the game itself):
  std::vector<std::string> notes;  // what the player should know before playing, a sentence each ("No music: ...")
  std::string user;     // the game's user folder (its saves and settings), when it has one
  int play = 0;         // kPlayOptions when it takes the launcher's start options (below)
};
constexpr int kServedSetup = 1;
// A game whose status says `"play": 1` takes, besides its own: --windowed, --window WxH, --nosound, --hidden,
// --write-dir DIR, and --ready-file FILE (FILE made at its first frame; FILE.status, when there, one line for the
// player while it loads). A launcher passes them and may close at the first frame; without it, the game starts plain.
constexpr int kPlayOptions = 1;

std::string state_name(Status::State s);  // "missing", "ready", "incomplete"

// The JSON `--status` prints (one line).
std::string status_json(const Status& s);
// What status_json wrote, back (a launcher reading a game's answer). False on
// anything else: not JSON, no "kit", or a newer kit than this one reads.
bool parse_status(const std::string& json, Status* out, std::string* error = nullptr);

// A setup the launcher shows on its own screens (rekit/sdl/setup_serve.h): one JSON object a line on the game's
// stdout, `done` on the last.
struct SetupMessage {
  bool done = false;
  // Until then, how far it is.
  double fraction = 0;
  std::string stage, message;
  // Then the answer: what the ask says each is.
  bool ok = false, cancelled = false;
  std::string path;     // --search: a copy found, "" none
  std::string words;    // --look: what was found ("An installed copy of MechWarrior 1.")
  std::string into;     // --look: where an install goes unless a folder is picked
  bool in_place = false;  // --look: the game's own copy, already set up; its folder is in use (`where`)
  std::string where;    // --install: the folder now holding the game's files
  std::string summary;  // a sentence for the player
  std::string title, pick_words, find_sub, find_words;  // --describe: the find screen's words
  // --copies (2026-11-25): before `done`, a line for each copy of the game on this computer (not done, no progress):
  // its folder, its data's version, whether it is the copy in use, and what the game would lack with it.
  std::string copy;     // the copy's folder; "" on any other line
  std::string copy_data;
  bool copy_in_use = false;
  std::vector<std::string> copy_missing;
};
std::string setup_message_json(const SetupMessage& m);
bool parse_setup_message(const std::string& json, SetupMessage* out);

}  // namespace rekit
