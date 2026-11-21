#pragma once
#include <string>
#include <vector>

// Where an old game may be installed (from mw3-re): its installer's registry
// values, its default folders, the usual hand-picked ones, the Wine, Proton
// and Lutris prefixes and the Mac's CrossOver and Whisky bottles, and the
// program's own folder. The recipe names its keys and folders; folders only
// come back, the caller checks which game each holds.
namespace rekit {

struct SearchHints {
  // Under HKLM\SOFTWARE (32- and 64-bit views) and the user's VirtualStore
  // copy: "<key>" "<value>", e.g. {"MicroProse\\MechWarrior 3\\1.0", "Program"}.
  struct RegistryValue {
    std::string key, value;
  };
  std::vector<RegistryValue> registry;
  // App Paths entries (the program's file name, "mech3.exe"): their "Path" and default values.
  std::vector<std::string> app_paths;
  // The installer's default folders, '/'-separated, under Program Files (and a prefix's), and the
  // system drive's top: "MicroProse/MechWarrior 3".
  std::vector<std::string> installer_folders;
  // Hand-picked places, under the system drive's top (a prefix's drive_c) and the home folder:
  // "Games/MechWarrior 3", "MW3".
  std::vector<std::string> other_folders;
};

// The folders in order, each once: the registry's, the installer's defaults,
// the hand-picked ones (on Linux and the Mac those of every Windows prefix
// found, then the home folder's), then `program_dir` and the folder above it
// (on the Mac, beside the .app too). Windows-only parts are skipped elsewhere.
std::vector<std::string> install_candidates(const SearchHints& hints, const std::string& program_dir);

// The folder an installer's value names: a folder, or a program's full path
// (InstallShield's App Paths default), quotes and spaces trimmed.
std::string install_of_program_value(std::string value);

}  // namespace rekit
