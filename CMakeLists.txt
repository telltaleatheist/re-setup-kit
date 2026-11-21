cmake_minimum_required(VERSION 3.20)
project(re_setup_kit C CXX)

# Built on its own (its tests) or inside a game (add_subdirectory): target rekit.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rekit STATIC
  src/archive.cpp
  src/cue_audio.cpp
  src/disc.cpp
  src/files.cpp
  src/hash.cpp
  src/installshield.cpp
  src/search.cpp
  src/sources.cpp
  src/status.cpp
  src/zip.cpp
  third_party/miniz/miniz_tinfl.c
)
# third_party: miniz's inflate (tinfl), public for a game that inflates too.
target_include_directories(rekit PUBLIC include third_party)
if(WIN32)
  target_link_libraries(rekit PUBLIC advapi32)  # the install search reads the registry
endif()
set_source_files_properties(third_party/miniz/miniz_tinfl.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")

# libarchive (zip, 7z, RAR, tar, ISO 9660) with zlib and liblzma. A game may
# point REKIT_LIBARCHIVE_INCLUDE/_LIB at its own build; else the usual places.
# Without it, setup reads folders, drives, .iso and .cue only.
if(NOT REKIT_LIBARCHIVE_INCLUDE)
  find_path(REKIT_LIBARCHIVE_INCLUDE archive.h
            HINTS /opt/homebrew/opt/libarchive/include /usr/local/opt/libarchive/include)
endif()
if(NOT REKIT_LIBARCHIVE_LIB)
  find_library(REKIT_LIBARCHIVE_LIB NAMES archive archive_static
               HINTS /opt/homebrew/opt/libarchive/lib /usr/local/opt/libarchive/lib)
endif()
if(REKIT_LIBARCHIVE_INCLUDE AND REKIT_LIBARCHIVE_LIB)
  target_include_directories(rekit PUBLIC "${REKIT_LIBARCHIVE_INCLUDE}")
  target_link_libraries(rekit PUBLIC "${REKIT_LIBARCHIVE_LIB}")
  if(REKIT_LIBARCHIVE_LIB MATCHES "\\.(a|lib)$")
    target_compile_definitions(rekit PUBLIC LIBARCHIVE_STATIC)
  endif()
  target_compile_definitions(rekit PUBLIC REKIT_HAVE_LIBARCHIVE=1)
  message(STATUS "rekit: libarchive ${REKIT_LIBARCHIVE_LIB}")
else()
  target_compile_definitions(rekit PUBLIC REKIT_HAVE_LIBARCHIVE=0)
  message(STATUS "rekit: no libarchive (folders, drives, .iso and .cue only)")
endif()

# The SDL layer (pickers, helper programs; later the setup screen): rekit_sdl, when SDL3 is there.
if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 CONFIG QUIET)
endif()
if(TARGET SDL3::SDL3)
  add_library(rekit_sdl STATIC src/sdl/child.cpp)
  target_link_libraries(rekit_sdl PUBLIC rekit SDL3::SDL3)
  message(STATUS "rekit: SDL layer (rekit_sdl)")
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_executable(rekit_tests tests/main.cpp tests/test_archive.cpp tests/test_cue_audio.cpp tests/test_cabinets.cpp tests/test_files.cpp tests/test_sources.cpp tests/test_search.cpp tests/test_status.cpp)
  target_include_directories(rekit_tests PRIVATE tests)
  target_link_libraries(rekit_tests PRIVATE rekit)
  target_compile_definitions(rekit_tests PRIVATE REKIT_TEST_TMP_ROOT="${CMAKE_BINARY_DIR}")
  if(TARGET rekit_sdl)
    target_sources(rekit_tests PRIVATE tests/test_sdl.cpp)
    target_link_libraries(rekit_tests PRIVATE rekit_sdl)
  endif()
endif()
