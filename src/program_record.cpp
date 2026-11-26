#include "rekit/program_record.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace rekit {
namespace {

std::string read_text(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::string value(const std::string& text, const std::string& key) {
  for (size_t at = 0; at < text.size();) {
    const size_t end = std::min(text.find('\n', at), text.size());
    std::string line = text.substr(at, end - at);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
    at = end + 1;
  }
  return {};
}

}  // namespace

std::string self_path() {
  std::error_code ec;
#if defined(_WIN32)
  wchar_t buf[4096];
  const DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
  return n > 0 && n < 4096 ? fs::path(std::wstring(buf, n)).string() : std::string();
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t n = sizeof buf;
  if (_NSGetExecutablePath(buf, &n) != 0) return {};
  const std::string p = fs::weakly_canonical(fs::path(buf), ec).string();
  return ec || p.find("/AppTranslocation/") != std::string::npos ? std::string() : p;
#else
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (ec) return {};
  const char *ai = std::getenv("APPIMAGE"), *mount = std::getenv("APPDIR");
  if (ai && *ai && mount && *mount && self.string().rfind(std::string(mount) + "/", 0) == 0)
    return fs::is_regular_file(ai, ec) ? std::string(ai) : std::string();
  return self.string();
#endif
}

bool launchers_own(const std::string& program) {
  std::error_code ec;
  fs::path at = fs::weakly_canonical(program, ec);
  for (int up = 0; up < 8 && at.has_parent_path() && at != at.parent_path(); ++up) {
    at = at.parent_path();  // the package's own layout inside the game's folder: <x>.app/Contents/MacOS, squashfs-root/usr/bin
    if (fs::is_regular_file(at / kInstalledMark, ec)) return true;
  }
  return false;
}

bool record_program(const std::string& user_folder, const std::string& key, const std::string& version) {
  if (user_folder.empty() || key.empty()) return false;
  const std::string self = self_path();
  if (self.empty() || launchers_own(self)) return false;
  const std::string text = std::string("# Where this game is, for the BattleTech launcher (it links a copy installed without it).\n") +
                           "game=" + key + "\nprogram=" + self + "\nversion=" + version + "\n";
  const fs::path p = fs::path(user_folder) / kProgramRecord;
  if (read_text(p) == text) return true;
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f << text;
  return static_cast<bool>(f);
}

ProgramRecord read_program_record(const std::string& user_folder) {
  ProgramRecord r;
  if (user_folder.empty()) return r;
  const std::string text = read_text(fs::path(user_folder) / kProgramRecord);
  r.game = value(text, "game"), r.program = value(text, "program"), r.version = value(text, "version");
  return r;
}

}  // namespace rekit
