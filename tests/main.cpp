// The kit's tests. REKIT_TEST=a,b runs only the tests whose names contain any of them.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <string>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "check.h"

namespace check {
std::vector<Test>& registry() {
  static std::vector<Test> tests;
  return tests;
}
int failures = 0;
}  // namespace check

const char* rekit_test_tmp() {
  static const std::string* dir = new std::string([] {
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const long me = static_cast<long>(_getpid());
#else
    const long me = static_cast<long>(getpid());
#endif
    const fs::path d = fs::path(REKIT_TEST_TMP_ROOT) / "test-tmp" / std::to_string(me);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d.generic_string();
  }());
  static const bool cleanup = (std::atexit([] {
    std::error_code ec;
    std::filesystem::remove_all(*dir, ec);
  }), true);
  (void)cleanup;
  return dir->c_str();
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const char* only = std::getenv("REKIT_TEST");
  auto wanted = [only](const char* name) {
    if (!only || !*only) return true;
    std::string list = only;
    for (size_t at = 0; at <= list.size();) {
      const size_t end = std::min(list.find(',', at), list.size());
      const std::string part = list.substr(at, end - at);
      if (!part.empty() && std::strstr(name, part.c_str())) return true;
      at = end + 1;
    }
    return false;
  };
  int ran = 0;
  for (const check::Test& t : check::registry()) {
    if (!wanted(t.name)) continue;
    const int before = check::failures;
    t.fn();
    ++ran;
    std::printf("%s %s\n", check::failures == before ? "ok  " : "FAIL", t.name);
  }
  std::printf("%d tests, %d failed checks\n", ran, check::failures);
  return check::failures ? 1 : 0;
}
