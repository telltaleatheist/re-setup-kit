// The SDL layer (rekit/sdl): no picker in an unattended run; a helper's whole output read through its pipe.
#include <string>

#include "check.h"
#include "rekit/sdl/child.h"
#include "rekit/sdl/pickers.h"

TEST(sdl_no_picker_opens_unattended) {
  int opened = 0, cancelled = 0;
  auto keep_open = rekit::sdl::picker_opener();
  auto keep_un = rekit::sdl::unattended();
  rekit::sdl::picker_opener() = [&](const rekit::sdl::PickerAsk&) { ++opened; };
  rekit::sdl::PickerAsk a;
  a.user = &cancelled;
  a.done = [](void* u, const char* const* files, int) {
    if (files && !files[0]) ++*static_cast<int*>(u);
  };
  rekit::sdl::unattended() = [] { return true; };
  EXPECT(!rekit::sdl::open_picker(a) && opened == 0 && cancelled == 1);
  rekit::sdl::unattended() = [] { return false; };
  EXPECT(rekit::sdl::open_picker(a) && opened == 1 && cancelled == 1);
  rekit::sdl::picker_opener() = keep_open;
  rekit::sdl::unattended() = keep_un;
}

#if !defined(_WIN32)
// A child that writes slowly, in pieces, is read to its end (not to the first empty read).
TEST(sdl_child_output_read_to_the_end) {
  const char* args[] = {"/bin/sh", "-c", "for i in 1 2 3; do printf 'line %s\\n' $i; sleep 0.2; done", nullptr};
  SDL_Process* p = rekit::sdl::start_child(args);
  EXPECT(p != nullptr);
  if (!p) return;
  std::string out;
  char buf[64];
  for (size_t n; (n = rekit::sdl::read_child(SDL_GetProcessOutput(p), buf, sizeof buf)) > 0;) out.append(buf, n);
  int code = -1;
  SDL_WaitProcess(p, true, &code);
  SDL_DestroyProcess(p);
  EXPECT(out == "line 1\nline 2\nline 3\n" && code == 0);
}
#endif
