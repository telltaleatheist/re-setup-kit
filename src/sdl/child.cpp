#include "rekit/sdl/child.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rekit::sdl {

SDL_Process* start_child(const char* const* args) {
#if defined(_WIN32)
  if (!GetConsoleWindow()) {
    const SDL_PropertiesID p = SDL_CreateProperties();
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* proc = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    return proc;
  }
#endif
  return SDL_CreateProcess(args, true);
}

size_t read_child(SDL_IOStream* io, void* buf, size_t n) {
  for (;;) {
    const size_t got = io ? SDL_ReadIO(io, buf, n) : 0;
    if (got > 0 || !io || SDL_GetIOStatus(io) != SDL_IO_STATUS_NOT_READY) return got;
    SDL_Delay(10);
  }
}

}  // namespace rekit::sdl
