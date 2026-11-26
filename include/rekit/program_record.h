#pragma once
#include <string>

// Where a game's program is, for a launcher that finds copies installed without it (from mw3-re's sdl_games,
// 2026-11-25; its launcher.md, Copies installed without the launcher). Each released game keeps this file in its user
// folder: "game=<key>", "program=<the file a player starts>", "version=<its tag>" lines, under a comment line. The
// file's name and lines are the ones the -re games have always written.
namespace rekit {

constexpr const char* kProgramRecord = "rebuild-program.txt";
// In the folder above a program a launcher installed (its games folder's <key>/): that copy is the launcher's own.
constexpr const char* kInstalledMark = "rebuild-game.txt";

// This program's file as a player starts it: $APPIMAGE when it runs from that AppImage's mount (not one it inherited
// from a launcher's), else its own file; empty when it runs from somewhere that goes away (the Mac's App Translocation).
std::string self_path();

// Whether `program` is in a game's folder a launcher made (kInstalledMark in a folder above it).
bool launchers_own(const std::string& program);

// The record for game `key` at `version`, written into `user_folder` when it changed. A caller writes it for a
// released build only: a development one never links itself. Nothing is written for a launcher's own copy, or when
// this program's path is unknown. True when the record is there and says this program.
bool record_program(const std::string& user_folder, const std::string& key, const std::string& version);

struct ProgramRecord {
  std::string game, program, version;  // empty when the folder has none
};
ProgramRecord read_program_record(const std::string& user_folder);

}  // namespace rekit
