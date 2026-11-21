#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Where a game is in whatever a player points setup at (from mw3-re): an
// installed copy (its folder or its program), the disc or its drive, a disc
// image (.iso, or a .cue with its .bin tracks), or any of those inside
// archives at any depth. The kit knows no game: the recipe names its clue
// files and judges their bytes; the walk finds them, reading archives
// forward once and a disc image inside one only up to its clue.
namespace rekit {

// One way into the next container, from the picked path: an archive entry
// opened as an archive, or as a disc image (raw 2352-byte sectors or 2048).
struct Step {
  enum Kind { kArchive, kImage, kRawImage } kind = kArchive;
  std::string entry;     // the entry's path in the container before it
  int raw_offset = 16;   // kRawImage: the user data's place in a sector (MODE1 16, MODE2 24)
};

// A recipe's verdict on a clue file's bytes.
struct Identity {
  std::string game;     // the recipe's name for what it is; "" when not the game
  std::string version;  // "" unknown
  int rank = 0;         // which wins when a source holds several (an install gets +1 over a disc)
  std::string why;      // when not the game, why (for the log)
};

// A file that shows where a game is.
struct Clue {
  enum Kind { kInstall, kDisc } kind = kInstall;
  // Relative to the game's folder, lower case, '/'-separated ("zbd/reader.zbd",
  // "data1.hdr"). A disc clue inside a disc image is looked for in the
  // image's top folder only, so its path has no '/'.
  std::string path;
  size_t limit = 8u << 20;  // read at most this many bytes of it
  std::function<Identity(const std::vector<uint8_t>& bytes)> judge;
};

struct SourceRules {
  std::vector<Clue> clues;
  std::vector<std::string> programs;  // file names (lower case) whose pick means their folder ("mech3.exe")
};

struct Found {
  enum Kind { kNothing, kInstall, kDisc } kind = kNothing;
  std::string game;            // the recipe's Identity::game
  int rank = 0;
  std::string path;            // what was picked
  std::vector<Step> steps;     // from it to the container holding the game
  std::string root;            // the game's folder in that container ("" its top, else ending '/')
  bool on_disk = false;        // the game is in a plain folder or image file: no archive to stream
  std::string version;
  std::string label;           // a disc image's volume label
  std::vector<std::string> audio;    // a cue's AUDIO tracks' entries (or files), in track order
  std::vector<int> audio_track;      // their track numbers
  std::vector<int64_t> audio_start;  // their INDEX 01, in bytes into their file (the pregap left out)
  std::string data_track;      // a cue's data track's entry (or file)
  int raw_offset = 16;         // the data track's sectors (2352: 16 or 24; 2048: 0)
  bool raw = false;
  std::string detail;          // what was read, for the log
  bool cancelled = false;      // the look was stopped (SourceProgress)
};

// The share of the picked file read so far (0..1, an archive's) and the
// entry being looked at; false stops the look (`cancelled` set).
using SourceProgress = std::function<bool(double fraction, const std::string& entry)>;

// Walks `path` (a folder, a file, an archive) for the rules' clues and gives
// the best-ranked find.
Found find_source(const std::string& path, const SourceRules& rules, const SourceProgress& progress = nullptr);

// `rel` ('/'-separated) under `dir` in any case, as it is on disk; "" when absent.
std::string any_case_path(const std::string& dir, const std::string& rel);

}  // namespace rekit
