#include "rekit/sdl/setup_original.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "rekit/hash.h"
#include "rekit/sign.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace rekit::sdl {

std::string curl_program() {
  std::error_code ec;
#if defined(_WIN32)
  if (const char* root = std::getenv("SystemRoot")) {
    const fs::path p = fs::path(root) / "System32" / "curl.exe";
    if (fs::is_regular_file(p, ec)) return p.string();
  }
  return {};
#else
  for (const char* p : {"/usr/bin/curl", "/bin/curl", "/usr/local/bin/curl"})
    if (fs::is_regular_file(p, ec)) return p;
  return {};
#endif
}

bool fetch_url(const std::string& url, const std::string& to, uint64_t size, int seconds, bool allow_file,
               const std::function<bool(double)>& progress, std::string* error, bool* cancelled, bool fresh) {
  *cancelled = false;
  const std::string curl = curl_program();
  if (curl.empty()) return *error = "this computer has no curl to download with", false;
  const bool file = url.rfind("file:///", 0) == 0;
  // REKIT_LOCAL_HTTP (checks): plain http to this computer only, a stand-in server that drops the connection.
  const bool local = SDL_getenv("REKIT_LOCAL_HTTP") && url.rfind("http://127.0.0.1:", 0) == 0;
  if (file ? !allow_file : !local && url.rfind("https://", 0) != 0) return *error = "not an https:// address", false;
  std::error_code ec;
  const std::string part = to + ".part";
  fs::remove(part, ec);
  const auto got_bytes = [&]() { return fs::exists(part, ec) ? static_cast<uint64_t>(fs::file_size(part, ec)) : uint64_t{0}; };
  const auto stop = [&]() {
    return progress && !progress(size ? std::min(1.0, static_cast<double>(got_bytes()) / static_cast<double>(size)) : 0.0);
  };
  // One curl run, from the part's end when `resume` (--continue-at -): its exit code, -2 for a cancel.
  const auto attempt = [&](bool resume, std::string* said) {
    std::vector<std::string> args = {curl, "--silent", "--show-error", "--fail", "--location", "--connect-timeout", "20",
                                     "--speed-limit", "1024", "--speed-time", "60", "--proto", file ? "=file" : local ? "=http" : "=https",
                                     "--proto-redir", "=https", "--user-agent", "re-setup-kit", "--output", part};
    if (seconds > 0) args.insert(args.end(), {"--max-time", std::to_string(seconds)});
    if (fresh) args.insert(args.end(), {"--header", "Cache-Control: no-cache", "--header", "Pragma: no-cache"});
    if (resume) args.insert(args.end(), {"--continue-at", "-"});
    args.push_back(url);
    if (const char* to_file = SDL_getenv("REKIT_CURL_ARGS_TO"))
      if (std::ofstream f(to_file, std::ios::app); f)
        for (const std::string& a : args) f << a << "\n";
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);  // curl's reason
#if defined(_WIN32)
    // A GUI build: no console window for curl (child.h; its exit code then reads 0, so the file is what is judged).
    if (!GetConsoleWindow()) SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    SDL_Process* p = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!p) return *said = std::string("curl could not be started (") + SDL_GetError() + ")", -1;
    int code = -1;
    SDL_IOStream* io = SDL_GetProcessOutput(p);
    for (;;) {  // asked before each wait, so even a quick copy can be stopped
      if (stop()) {
        SDL_KillProcess(p, true);
        SDL_WaitProcess(p, true, &code);
        SDL_DestroyProcess(p);
        return -2;
      }
      if (SDL_WaitProcess(p, false, &code)) break;
      char buf[256];
      const size_t n = io ? SDL_ReadIO(io, buf, sizeof buf) : 0;
      said->append(buf, n);
      if (n == 0) SDL_Delay(50);
    }
    if (io)
      for (char buf[256]; const size_t n = SDL_ReadIO(io, buf, sizeof buf);) said->append(buf, n);
    SDL_DestroyProcess(p);
    return code;
  };
  // A dropped connection (2026-11-27): tried again after 5 s from where the part ends, while each try gets further
  // (at most 5, the first retry always); a server that will not resume (curl 33, HTTP 416) starts it over once. A
  // download of a known size that came short counts as dropped whatever curl said (a GUI build reads its code as 0).
  // Not for a capped fetch (`seconds`: a catalog). The caller still checks the whole file (size, SHA-256).
  std::string said;
  int code = -1;
  bool resume = false, restarted = false;
  uint64_t before = 0;
  for (int tries = 1;; ++tries) {
    said.clear();
    code = attempt(resume, &said);
    if (code == -2) return fs::remove(part, ec), *cancelled = true, *error = "canceled", false;
    const uint64_t now = got_bytes();
    const bool short_file = size > 0 && now < size;
    if ((code == 0 && !short_file) || code == -1 || file || seconds > 0) break;
    const bool no_resume = code == 33 || (code == 22 && said.find("416") != std::string::npos);
    const bool dropped = short_file || code == 6 || code == 7 || code == 18 || code == 28 || code == 35 || code == 52 || code == 55 || code == 56;
    if (resume && no_resume && !restarted) {
      restarted = true, resume = false, fs::remove(part, ec);
      continue;
    }
    if (!dropped || tries >= 5 || (tries > 1 && now <= before)) break;
    before = now, resume = now > 0;
    for (int w = 0; w < 50; ++w) {  // 5 s, a cancel still heard
      if (stop()) return fs::remove(part, ec), *cancelled = true, *error = "canceled", false;
      SDL_Delay(100);
    }
  }
  while (!said.empty() && (said.back() == '\n' || said.back() == '\r')) said.pop_back();
  if (said.rfind("curl: ", 0) == 0) said.erase(0, 6);
  if (const size_t paren = said.find(") "); said.rfind("(", 0) == 0 && paren != std::string::npos) said.erase(0, paren + 2);
  if (code != 0 || !fs::exists(part, ec)) {  // a short one kept: the caller says how short (its size check)
    const std::string why = !said.empty() ? said : code != 0 ? "curl stopped (" + std::to_string(code) + ")" : "nothing came";
    fs::remove(part, ec);
    return *error = why, false;
  }
  fs::remove(to, ec);
  fs::rename(part, to, ec);
  if (ec) {
    fs::remove(part, ec);
    return *error = "the download could not be kept: " + ec.message(), false;
  }
  return true;
}

std::string original_dir(const std::string& temp, const std::string& game_id, std::string* error) {
  std::error_code ec;
  const fs::path base = temp.empty() ? fs::temp_directory_path(ec) : fs::path(temp);
  if (base.empty()) return *error = "this computer has no temp folder", std::string();
  const fs::path dir = base / ("rekit-original-" + game_id);
  if (fs::exists(dir, ec) && !fs::exists(dir / kOriginalMark, ec) && !fs::is_empty(dir, ec))
    return *error = dir.string() + " is not the kit's folder, so nothing was written to it", std::string();
  fs::create_directories(dir, ec);
  std::ofstream(dir / kOriginalMark) << "re-setup-kit: a game's original files while they download; removed after.\n";
  if (!fs::exists(dir / kOriginalMark, ec)) return *error = dir.string() + " could not be made", std::string();
  return dir.string();
}

void tidy_original_dir(const std::string& dir) {
  std::error_code ec;
  if (dir.empty() || !fs::exists(fs::path(dir) / kOriginalMark, ec)) return;  // only the kit's own
  for (const auto& e : fs::directory_iterator(dir, ec)) {
    const std::string n = e.path().filename().string();
    if (n != kOriginalMark) return;  // something else there: kept, with its mark
  }
  fs::remove(fs::path(dir) / kOriginalMark, ec);
  fs::remove(dir, ec);
}

bool catalog_sources(const std::vector<std::string>& catalogs, const std::vector<std::string>& keys, const std::string& key,
                     const std::string& dir, bool allow_file, const std::function<bool(double)>& go_on,
                     std::vector<OriginalSource>* out, std::string* why, const std::string& game_version) {
  out->clear(), why->clear();
  const auto read = [](const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  };
  std::string reasons;
  for (const std::string& catalog : catalogs) {
    const fs::path to = fs::path(dir) / "catalog.json", sig = fs::path(dir) / "catalog.json.sig";
    bool cancelled = false;
    std::string error, bad;
    std::error_code ec;
    if (!fetch_url(catalog, to.string(), 0, 20, allow_file, go_on, &error, &cancelled, true)) {
      bad = error;
    } else if (!fetch_url(catalog + ".sig", sig.string(), 0, 20, allow_file, go_on, &error, &cancelled, true)) {
      bad = "no signature (" + error + ")";
    } else {
      const std::string text = read(to);
      if (rekit::signed_by(text, read(sig), keys).empty()) {
        bad = "its signature does not check against the game's keys";
      } else {
        fs::remove(to, ec), fs::remove(sig, ec);
        return read_original(text, key, allow_file, out, why, game_version);
      }
    }
    fs::remove(to, ec), fs::remove(sig, ec);
    reasons += (reasons.empty() ? "" : "; ") + catalog + ": " + bad;
    if (cancelled) break;
  }
  *why = reasons.empty() ? "no catalog to ask" : reasons;
  return false;
}

InstallResult fetch_original(const std::vector<OriginalSource>& sources, const std::string& dir, bool allow_file,
                             const ProgressFn& progress) {
  InstallResult r;
  std::string why;
  std::error_code ec;
  for (size_t n = 0; n < sources.size(); ++n) {
    const OriginalSource& s = sources[n];
    const fs::path file = fs::path(dir) / s.name;
    const double from = static_cast<double>(n) / static_cast<double>(sources.size());
    const double span = 1.0 / static_cast<double>(sources.size());
    bool cancelled = false;
    std::string bad;
    const auto bar = [&](double f) { return progress({"Getting the original game files", from + span * 0.9 * f, s.name}); };
    if (!fetch_url(s.url, file.string(), s.size, 0, allow_file, bar, &bad, &cancelled)) {
      if (cancelled) return fs::remove(file, ec), r.cancelled = true, r.summary = "The download was canceled.", r;
    } else if (const uint64_t got = static_cast<uint64_t>(fs::file_size(file, ec)); ec || got != s.size) {
      bad = "it is " + std::to_string(ec ? 0 : got) + " bytes, not " + std::to_string(s.size);
    } else if (!progress({"Checking the original game files", from + span * 0.95, s.name})) {
      return fs::remove(file, ec), r.cancelled = true, r.summary = "The download was canceled.", r;
    } else if (sha256_file_hex(file.string()) != s.sha256) {
      bad = "it does not match its SHA-256";
    } else {
      r.ok = true, r.install_dir = file.string(), r.summary = s.name;
      return r;
    }
    why += (why.empty() ? "" : "; ") + bad;
    fs::remove(file, ec), fs::remove(file.string() + ".part", ec);
  }
  r.summary = why.empty() ? "no source" : why;
  return r;
}

}  // namespace rekit::sdl
