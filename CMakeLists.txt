cmake_minimum_required(VERSION 3.20)
project(re_setup_kit C CXX)

# Built on its own (its tests) or inside a game (add_subdirectory): target rekit.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rekit STATIC
  src/archive.cpp
  src/backup.cpp
  src/cue_audio.cpp
  src/data_folder.cpp
  src/disc.cpp
  src/family_folder.cpp
  src/files.cpp
  src/game_folder.cpp
  src/hash.cpp
  src/installshield.cpp
  src/manifest.cpp
  src/original.cpp
  src/program_record.cpp
  src/search.cpp
  src/sign.cpp
  src/sources.cpp
  src/status.cpp
  src/update.cpp
  src/zip.cpp
  third_party/miniz/miniz_tinfl.c
  third_party/monocypher/monocypher.c
  third_party/monocypher/monocypher-ed25519.c
)
# Position-independent: a game may link the kit into a shared library (mech3-pm-re's libpmprobe.so; GNU ld refuses
# a non-PIC static archive there, found by MW1's first Linux build 2026-11-23).
set_target_properties(rekit PROPERTIES POSITION_INDEPENDENT_CODE ON)
# third_party: miniz's inflate (tinfl), public for a game that inflates too.
target_include_directories(rekit PUBLIC include third_party)
if(WIN32)
  target_link_libraries(rekit PUBLIC advapi32)  # the install search reads the registry
endif()
set_source_files_properties(third_party/miniz/miniz_tinfl.c third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")

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
    # A static libarchive needs its zlib and liblzma linked after it (GNU ld reads the line in order; a game's own
    # copies earlier on the line do not count): those beside it first, else the system's.
    get_filename_component(REKIT_LIBARCHIVE_DIR "${REKIT_LIBARCHIVE_LIB}" DIRECTORY)
    find_library(REKIT_LZMA_LIB NAMES lzma liblzma HINTS "${REKIT_LIBARCHIVE_DIR}")
    find_library(REKIT_Z_LIB NAMES z zlibstatic zlib zs HINTS "${REKIT_LIBARCHIVE_DIR}")
    foreach(dep REKIT_LZMA_LIB REKIT_Z_LIB)
      if(${dep})
        target_link_libraries(rekit PUBLIC "${${dep}}")
      endif()
    endforeach()
  endif()
  target_compile_definitions(rekit PUBLIC REKIT_HAVE_LIBARCHIVE=1)
  message(STATUS "rekit: libarchive ${REKIT_LIBARCHIVE_LIB}")
else()
  target_compile_definitions(rekit PUBLIC REKIT_HAVE_LIBARCHIVE=0)
  message(STATUS "rekit: no libarchive (folders, drives, .iso and .cue only)")
endif()

# The SDL layer (pickers, helper programs, the setup screen): rekit_sdl, when SDL3 is there.
if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 CONFIG QUIET)
endif()
if(TARGET SDL3::SDL3)
  # The setup screen draws its text with stb_truetype and reads PNG art with stb_image (third_party/stb). Its default
  # look, the launcher's fonts and sounds (assets/), is built in.
  include(cmake/embed_assets.cmake)
  set(REKIT_ASSETS_CPP "${CMAKE_CURRENT_BINARY_DIR}/rekit_assets.cpp")
  rekit_embed_assets("${REKIT_ASSETS_CPP}"
    kit_font_bold "${CMAKE_CURRENT_SOURCE_DIR}/assets/SairaCondensed-Bold.ttf"
    kit_font_body "${CMAKE_CURRENT_SOURCE_DIR}/assets/SairaCondensed-Medium.ttf"
    kit_click_wav "${CMAKE_CURRENT_SOURCE_DIR}/assets/click.wav"
    kit_hover_wav "${CMAKE_CURRENT_SOURCE_DIR}/assets/hover.wav")
  add_library(rekit_sdl STATIC src/sdl/child.cpp src/sdl/game_menu.cpp src/sdl/setup_ops.cpp src/sdl/setup_original.cpp src/sdl/setup_screen.cpp src/sdl/setup_serve.cpp src/sdl/setup_text.cpp src/sdl/theme.cpp src/sdl/update.cpp "${REKIT_ASSETS_CPP}")
  target_include_directories(rekit_sdl PRIVATE src/sdl)
  # What a game's package must ship beside its program for rekit_sdl: the built-in fonts' licence (OFL). A game's
  # packaging copies every file in this list, so all games ship the same notices (THIRD-PARTY.md).
  set(REKIT_NOTICE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/OFL.txt" CACHE INTERNAL "files a package with rekit_sdl ships")
  target_link_libraries(rekit_sdl PUBLIC rekit SDL3::SDL3)
  set_target_properties(rekit_sdl PROPERTIES POSITION_INDEPENDENT_CODE ON)
  message(STATUS "rekit: SDL layer (rekit_sdl)")
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_executable(rekit_tests tests/main.cpp tests/test_archive.cpp tests/test_cue_audio.cpp tests/test_cabinets.cpp tests/test_files.cpp tests/test_family_folder.cpp tests/test_game_folder.cpp tests/test_sources.cpp tests/test_search.cpp tests/test_status.cpp tests/test_backup.cpp tests/test_update.cpp tests/test_names.cpp tests/test_original.cpp tests/test_sign.cpp tests/test_data_folder.cpp tests/test_program_record.cpp)
  target_include_directories(rekit_tests PRIVATE tests)
  target_sources(rekit_tests PRIVATE tests/test_manifest.cpp)  # the install record (CHECK MY FILES)
  target_link_libraries(rekit_tests PRIVATE rekit)
  target_compile_definitions(rekit_tests PRIVATE REKIT_TEST_TMP_ROOT="${CMAKE_BINARY_DIR}")
  if(TARGET rekit_sdl)
    target_sources(rekit_tests PRIVATE tests/test_sdl.cpp tests/test_game_menu.cpp tests/test_setup_screen.cpp tests/test_setup_serve.cpp tests/test_update_sdl.cpp tests/test_update_menu.cpp)
    target_link_libraries(rekit_tests PRIVATE rekit_sdl)
  endif()
endif()

# The update engine's demo game for real-system runs (tools/update_demo): two builds, v1.2.0 and v1.4.0, signed with
# the test keys only. Off unless asked for.
option(REKIT_UPDATE_DEMO "build the update demo game" OFF)
if(REKIT_UPDATE_DEMO AND TARGET rekit_sdl)
  foreach(v 1.2.0 1.4.0)
    string(REPLACE "." "_" n ${v})
    add_executable(rekit_update_demo_${n} tools/update_demo/main.cpp)
    target_include_directories(rekit_update_demo_${n} PRIVATE tests)
    target_compile_definitions(rekit_update_demo_${n} PRIVATE REKIT_DEMO_TAG="v${v}")
    target_link_libraries(rekit_update_demo_${n} PRIVATE rekit_sdl)
    if(WIN32)  # the games' UTF-8 code page, so its paths are read as the games read theirs
      target_sources(rekit_update_demo_${n} PRIVATE tools/update_demo/utf8.manifest)
    endif()
  endforeach()
endif()
