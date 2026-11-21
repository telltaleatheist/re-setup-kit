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
};

std::string state_name(Status::State s);  // "missing", "ready", "incomplete"

// The JSON `--status` prints (one line).
std::string status_json(const Status& s);
// What status_json wrote, back (a launcher reading a game's answer). False on
// anything else: not JSON, no "kit", or a newer kit than this one reads.
bool parse_status(const std::string& json, Status* out, std::string* error = nullptr);

}  // namespace rekit
