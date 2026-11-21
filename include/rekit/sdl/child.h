#pragma once
// Helper programs (a movie converter, a format helper) started with their stdin and stdout piped to the game (from
// mw3-re's sdl_child and movie_import). On Windows, when the game has no console of its own (a GUI build), a console
// child would open a console window beside it; SDL starts it without one only as a "background" process
// (CREATE_NO_WINDOW), whose exit code then always reads 0 (callers judge the result by its output).
#include <cstddef>

#include <SDL3/SDL.h>

namespace rekit::sdl {

SDL_Process* start_child(const char* const* args);

// A child's output, read as it comes: SDL's process pipes are non-blocking, so a read with nothing there yet returns
// 0 with SDL_IO_STATUS_NOT_READY. This waits instead and returns 0 only at the end (or an error). Taking that first 0
// for the end once froze an importer's progress bar until it was done, and its CANCEL did nothing.
size_t read_child(SDL_IOStream* io, void* buf, size_t n);

}  // namespace rekit::sdl
