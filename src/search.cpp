#include "rekit/search.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rekit {
namespace fs = std::filesystem;
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string trimmed(std::string s) {
  while (!s.empty() && (s.back() == '/' || s.back() == '\\' || s.back() == ' ')) s.pop_back();
  return s;
}
std::string parent(const std::string& p) { return fs::path(p).parent_path().string(); }

#if defined(_WIN32)
// A string value, empty when the key or value is not there. `flags` picks the
// registry view (RRF_SUBKEY_WOW6432KEY: a 32-bit installer's).
std::string reg_string(HKEY root, const std::string& key, const char* name, DWORD flags) {
  char buf[1024];
  DWORD size = sizeof(buf);
  if (RegGetValueA(root, key.c_str(), name, RRF_RT_REG_SZ | flags, nullptr, buf, &size) != ERROR_SUCCESS) return {};
  return buf;
}
std::string win(std::string n) {
  for (char& c : n) c = c == '/' ? '\\' : c;
  return n;
}
#endif

}  // namespace

std::string install_of_program_value(std::string value) {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
  value = trimmed(value);
  const std::string l = lower(value);
  if (l.size() > 4 && l.compare(l.size() - 4, 4, ".exe") == 0) value = parent(value);
  return value;
}

std::vector<std::string> install_candidates(const SearchHints& h, const std::string& program_dir) {
  std::vector<std::string> out;
  auto add = [&](const std::string& d) {
    if (!d.empty() && std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
  };
  std::vector<std::string> all = h.installer_folders;
  all.insert(all.end(), h.other_folders.begin(), h.other_folders.end());
  auto installers = [&](const std::string& n) {
    return std::find(h.installer_folders.begin(), h.installer_folders.end(), n) != h.installer_folders.end();
  };
#if defined(_WIN32)
  // InstallShield's SOFTWARE\PUBLISHER\PRODUCT\VERSION values and App Paths
  // entry. A 32-bit installer on 64-bit Windows wrote the WOW6432Node view,
  // or, without admin rights, the user's VirtualStore copy.
  for (DWORD view : {static_cast<DWORD>(RRF_SUBKEY_WOW6432KEY), static_cast<DWORD>(RRF_SUBKEY_WOW6464KEY)}) {
    for (const SearchHints::RegistryValue& r : h.registry)
      add(install_of_program_value(reg_string(HKEY_LOCAL_MACHINE, "SOFTWARE\\" + r.key, r.value.c_str(), view)));
    for (const std::string& exe : h.app_paths) {
      const std::string app = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + exe;
      add(install_of_program_value(reg_string(HKEY_LOCAL_MACHINE, app, "Path", view)));
      add(install_of_program_value(reg_string(HKEY_LOCAL_MACHINE, app, nullptr, view)));
    }
  }
  for (const char* store : {"Software\\Classes\\VirtualStore\\MACHINE\\SOFTWARE\\WOW6432Node\\",
                            "Software\\Classes\\VirtualStore\\MACHINE\\SOFTWARE\\"})
    for (const SearchHints::RegistryValue& r : h.registry)
      add(install_of_program_value(reg_string(HKEY_CURRENT_USER, store + r.key, r.value.c_str(), 0)));
  // The installer's defaults under Program Files, then the system drive's top.
  for (const char* env : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"})
    if (const char* pf = std::getenv(env))
      for (const std::string& n : h.installer_folders) add(std::string(pf) + "\\" + win(n));
  const char* sd = std::getenv("SystemDrive");
  const std::string drive = sd ? sd : "C:";
  for (const std::string& n : all) add(drive + "\\" + win(n));
#else
  // A Windows install under Wine, Proton or Lutris, and on the Mac CrossOver's
  // and Whisky's bottles: each prefix's drive_c with the installer's defaults
  // and the hand-picked folders. A Proton prefix is a non-Steam game's
  // (compatdata/<id>/pfx).
  const char* home_env = std::getenv("HOME");
  const std::string home = home_env ? home_env : "";
  std::vector<std::string> prefixes;
  if (const char* wp = std::getenv("WINEPREFIX")) prefixes.push_back(wp);
  std::error_code ec;
  if (!home.empty()) {
    prefixes.push_back(home + "/.wine");
    for (const char* many : {"/.steam/steam/steamapps/compatdata", "/.local/share/Steam/steamapps/compatdata",
                             "/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/compatdata"})
      for (const fs::directory_entry& e : fs::directory_iterator(home + many, ec)) prefixes.push_back(e.path().string() + "/pfx");
    for (const char* many : {"/Games", "/Library/Application Support/CrossOver/Bottles",
                             "/Library/Containers/com.isaacmarovitz.Whisky/Bottles"})
      for (const fs::directory_entry& e : fs::directory_iterator(home + many, ec)) prefixes.push_back(e.path().string());
  }
  for (const std::string& pfx : prefixes)
    for (const std::string& n : all)
      for (const char* pf : {"/drive_c/Program Files (x86)/", "/drive_c/Program Files/", "/drive_c/"})
        if ((installers(n) || std::string(pf) == "/drive_c/") && fs::is_directory(pfx + pf + n, ec)) add(pfx + pf + n);
  if (!home.empty())
    for (const std::string& n : h.other_folders) add(home + "/" + n);
#endif
  (void)installers;
  // The program's folder and the one above (the rebuild beside or inside the
  // install); on the Mac, beside the .app.
  if (!program_dir.empty()) {
    const std::string b = trimmed(program_dir);
    add(b);
    add(parent(b));
    if (lower(b).find(".app/contents/resources") != std::string::npos) add(parent(parent(parent(b))));
  }
  return out;
}

}  // namespace rekit
