#include "rekit/sdl/setup_serve.h"
#include "rekit/settings.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "rekit/status.h"
#include "setup_ops.h"

namespace fs = std::filesystem;

namespace rekit::sdl {
namespace {

void send(const SetupMessage& m) {
  std::fputs((setup_message_json(m) + "\n").c_str(), stdout);
  std::fflush(stdout);
}

// A "cancel" line on stdin. Read by its descriptor on a thread of its own, left waiting at the exit (stdio's locks
// would hold the exit up); the flag outlives it.
std::shared_ptr<std::atomic<bool>> watch_cancel() {
  auto stop = std::make_shared<std::atomic<bool>>(false);
  std::thread([stop] {
    std::string line;
    char c;
    for (;;) {
#if defined(_WIN32)
      const int n = _read(0, &c, 1);
#else
      const ssize_t n = ::read(0, &c, 1);
#endif
      if (n <= 0) return;  // closed: whoever asked may be gone, the work goes on
      if (c != '\n' && c != '\r') {
        if (line.size() < 64) line += c;
        continue;
      }
      if (line == "cancel") return void(*stop = true);
      line.clear();
    }
  }).detach();
  return stop;
}

}  // namespace

bool serves_setup(const std::vector<std::string>& args) {
  if (args.empty()) return false;
  return args[0] == "--describe" || args[0] == "--search" || args[0] == "--copies" ||
         ((args[0] == "--look" || args[0] == "--install" || args[0] == "--add" || args[0] == "--first-run") && args.size() >= 2);
}

SetupResult serve_setup(const Game& g, const SetupOptions& o, const std::vector<std::string>& args) {
  SetupResult result;
  load_kit_settings(o.program_dir.empty() ? std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") : o.program_dir, o.user_folder,
                    nullptr);  // the download limits (rekit/settings.h)
  SetupMessage m;
  m.done = true;
  const std::string ask = args.empty() ? std::string() : args[0];
  if (ask == "--describe") {
    m.ok = true, m.title = g.title, m.pick_words = detail::what_to_pick(g), m.find_sub = detail::find_sub(g);
    m.find_words = detail::find_words(g), m.into = o.default_out;
    result.exit = kSetupReady;
    return send(m), result;
  }
  if (ask == "--search") {
    m.ok = true, m.path = detail::search_copy(g, o.program_dir);
    result.exit = kSetupReady;
    return send(m), result;
  }
  const bool hooked = ask == "--copies" ? static_cast<bool>(g.copies) : ask == "--add" ? static_cast<bool>(g.add)
                     : ask == "--first-run"                         ? static_cast<bool>(g.first_run)
                                                                    : true;
  if (!serves_setup(args) || !hooked) {
    result.exit = kSetupFailed, result.error = m.summary = "not a setup this game answers";
    return send(m), result;
  }
  if (ask == "--copies") {
    try {
      for (const CopyRow& c : g.copies(o)) {
        SetupMessage row;
        row.copy = c.path, row.copy_data = c.data, row.copy_in_use = c.in_use, row.copy_missing = c.missing;
        if (!row.copy.empty()) send(row);
      }
      m.ok = true, result.exit = kSetupReady;
    } catch (const std::exception& e) {
      result.exit = kSetupFailed, result.error = m.summary = e.what();
    }
    return send(m), result;
  }
  std::string folder;
  for (size_t i = 2; i + 1 < args.size(); ++i)
    if (args[i] == "--folder") folder = args[i + 1];
  const auto stop = watch_cancel();
  // At most one line every 50 ms, and each stage's first.
  auto last = std::chrono::steady_clock::time_point{};
  std::string stage;
  auto progress = [&](double from, double span) {
    return [&, from, span](const Progress& p) {
      const auto now = std::chrono::steady_clock::now();
      if (p.stage != stage || now - last >= std::chrono::milliseconds(50)) {
        stage = p.stage, last = now;
        SetupMessage q;
        q.fraction = from + span * p.fraction, q.stage = p.stage, q.message = p.message;
        send(q);
      }
      return !stop->load();
    };
  };
  if (ask == "--add" || ask == "--first-run") {
    InstallResult r;
    try {
      r = (ask == "--add" ? g.add : g.first_run)(args[1], o, progress(0, 1.0));
    } catch (const std::exception& e) {
      r = {}, r.summary = e.what();
    }
    m.ok = r.ok, m.cancelled = r.cancelled, m.summary = r.summary, m.where = r.install_dir;
    result.exit = r.ok ? kSetupReady : r.cancelled ? kSetupCancelled : kSetupFailed;
    if (result.exit == kSetupFailed) result.error = r.summary;
    return send(m), result;
  }
  const bool install = ask == "--install";
  std::string source, file;
  detail::pick_source(g, args[1], &source, &file);
  Found found;
  InstallResult r = detail::look(g, source, file, &found, progress(0, install ? 0.1 : 1.0));
  std::error_code ec;
  if (r.ok) {
    m.words = detail::found_words(g, found), m.path = found.path;
    m.in_place = found.kind == Found::kInstall && found.on_disk && !o.default_out.empty() && fs::equivalent(found.path, o.default_out, ec);
    if (m.in_place) {
      m.where = result.install_dir = found.path;
    } else if (install) {
      r = detail::install(g, found, folder, o, progress(0.1, 0.9));
      if (r.ok) m.where = result.install_dir = r.install_dir;
    } else {
      m.into = detail::out_dir(folder, o.default_out);
    }
  }
  m.ok = r.ok, m.cancelled = r.cancelled, m.summary = r.summary;
  result.exit = r.ok ? kSetupReady : r.cancelled ? kSetupCancelled : kSetupFailed;
  if (result.exit == kSetupFailed) result.error = r.summary;
  return send(m), result;
}

}  // namespace rekit::sdl
