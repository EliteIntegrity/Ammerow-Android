# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.22)
project(AmmerowAndroid LANGUAGES C)

# Debug builds stay debuggable but optimised: data parsing and level
# generation are far too slow at -O0 on phones.
add_compile_options($<$<CONFIG:Debug>:-O2>)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
# Source paths compiled into the libraries (assertion messages, debug
# information, and so the build IDs) are made relative to the checkout, so
# the APK does not depend on, or reveal, the folder it was built in.
add_compile_options("-ffile-prefix-map=${REPO_ROOT}=.")
set(THIRD_PARTY "${REPO_ROOT}/third_party")
if(NOT EXISTS "${THIRD_PARTY}/SDL/CMakeLists.txt")
    message(FATAL_ERROR "SDL sources missing: run tools/fetch-deps.ps1 (or tools/fetch-deps.sh)")
endif()

# SDL and its satellite libraries, built from the pinned sources.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL" SDL)

# PNG through the built-in stb decoder; no external image libraries.
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL_image" SDL_image)

# FreeType and HarfBuzz come from SDL_ttf's vendored submodules.
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ ON CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${THIRD_PARTY}/SDL_ttf" SDL_ttf)

# The game itself, in game/ (see game/sources.cmake).
set(GAME "${REPO_ROOT}/game")
include("${GAME}/sources.cmake")
set(CORE_SOURCES ${AMMEROW_CORE_SOURCES})
set(SDL3_SOURCES ${AMMEROW_SDL3_SOURCES})
list(TRANSFORM CORE_SOURCES PREPEND "${GAME}/")
list(TRANSFORM SDL3_SOURCES PREPEND "${GAME}/")

file(STRINGS "${GAME}/version" AMMEROW_VERSION LIMIT_COUNT 1)
string(STRIP "${AMMEROW_VERSION}" AMMEROW_VERSION)

# On Unix the game would keep settings and pref files in ~/.ammerow, using the
# passwd home directory, which on Android is the unwritable /data. Keep them in
# lib/user instead (config.h only supplies its default when this is unset).
# Saves and scores go to lib/save and lib/scores, as on desktop.
add_compile_definitions(PRIVATE_USER_PATH="./lib/user")

# Same split as the desktop build: the core never sees the frontend's defines.
add_library(AmmerowCore OBJECT ${CORE_SOURCES})
set_target_properties(AmmerowCore PROPERTIES C_STANDARD 99)
target_include_directories(AmmerowCore PRIVATE "${GAME}/src")
target_compile_definitions(AmmerowCore PRIVATE SOUND_SDL3 SOUND "BUILD_ID=${AMMEROW_VERSION}")
# The NDK makes -Wformat-security an error; ui-store.c passes a shopkeeper
# welcome string as a format (text_out(welcome)).
target_compile_options(AmmerowCore PRIVATE -Wno-error=format-security)
# The core includes SDL headers for the sound hooks.
target_link_libraries(AmmerowCore PRIVATE SDL3::SDL3)

# Ogg Vorbis audio (src/sdl3/audio-ogg.c): the package ships each sound as
# .ogg, decoded by SDL_mixer's SDL-adapted stb_vorbis (public domain).
if(NOT EXISTS "${THIRD_PARTY}/SDL_mixer/src/stb_vorbis/stb_vorbis.h")
    message(FATAL_ERROR "stb_vorbis missing: run tools/fetch-deps.ps1")
endif()
set_source_files_properties("${GAME}/src/sdl3/audio-ogg.c" PROPERTIES
    INCLUDE_DIRECTORIES "${THIRD_PARTY}/SDL_mixer/src"
    COMPILE_OPTIONS "-Wno-unused-value;-Wno-unused-function")

add_library(main SHARED
    $<TARGET_OBJECTS:AmmerowCore>
    ${SDL3_SOURCES}
    android/android-main.c
    android/context.c
    android/pins.c
    android/probe.c
    android/touch-text.c)
set_target_properties(main PROPERTIES C_STANDARD 99)
target_include_directories(main PRIVATE "${GAME}/src")
# Paths are relative to the working directory, which android-main.c sets to
# the app's internal storage after installing the runtime files there.
target_compile_definitions(main PRIVATE
    USE_SDL3 SOUND_SDL3 SOUND SDL3_AUDIO_OGG
    "BUILD_ID=${AMMEROW_VERSION}"
    DEFAULT_CONFIG_PATH="./lib/"
    DEFAULT_LIB_PATH="./lib/"
    DEFAULT_DATA_PATH="./lib/")
# The desktop main() becomes ammerow_main(), called by android-main.c once
# the runtime files are installed. SDL_MAIN_HANDLED stops SDL_main.h renaming it.
set_source_files_properties("${GAME}/src/main.c" PROPERTIES
    COMPILE_DEFINITIONS "SDL_MAIN_HANDLED;main=ammerow_main")
# The game's input waits and prompts pass through android/context.c, which
# tells the touch controls what the game is waiting for (and, in item lists,
# what the highlighted item allows). A wrap only catches calls from other
# files, which is where these prompts are called from.
target_link_options(main PRIVATE
    -Wl,--wrap=SDL_WaitEvent
    -Wl,--wrap=SDL_WaitEventTimeout
    -Wl,--wrap=askfor_aux
    -Wl,--wrap=get_character_name
    -Wl,--wrap=get_character_name_with_refresh
    -Wl,--wrap=textui_get_quantity_with_refresh
    -Wl,--wrap=target_set_interactive
    -Wl,--wrap=textui_target
    -Wl,--wrap=textui_process_click
    -Wl,--wrap=sdl3_presenter_point_to_cell
    -Wl,--wrap=do_cmd_help
    -Wl,--wrap=sdl3_monster_card_layout_compute
    -Wl,--wrap=sdl3_visual_init
    -Wl,--wrap=sdl3_visual_free
    -Wl,--wrap=sdl3_home_refresh_saves
    -Wl,--wrap=menu_select
    -Wl,--wrap=menu_dynamic_select
    -Wl,--wrap=use_store
    -Wl,--wrap=textui_textblock_show
    -Wl,--wrap=ui_store_screen_present
    -Wl,--wrap=ui_store_screen_present_confirmation
    -Wl,--wrap=ui_object_screen_present
    -Wl,--wrap=context_menu_object
    -Wl,--wrap=ui_birth_screen_present
    -Wl,--wrap=ui_birth_screen_present_points
    -Wl,--wrap=ui_birth_screen_present_roll
    -Wl,--wrap=ui_birth_screen_present_name
    -Wl,--wrap=ui_birth_screen_present_history
    -Wl,--wrap=ui_birth_screen_present_confirm
    -Wl,--wrap=sdl3_home_open
    -Wl,--wrap=move_cursor_relative
    -Wl,--wrap=sdl3_monster_card_configure
    -Wl,--wrap=Term_putstr
    -Wl,--wrap=Term_addstr
    -Wl,--wrap=text_out_c
    -Wl,--wrap=sdl3_ui_draw_text)
# Debug builds carry the layout probe (android/probe.c), which reports where
# each control and the game's text are drawn, for automated layout checks.
target_compile_definitions(main PRIVATE $<$<CONFIG:Debug>:AMMEROW_LAYOUT_PROBE>)
target_link_options(main PRIVATE
    $<$<CONFIG:Debug>:-Wl,--wrap=sdl3_font_draw>
    $<$<CONFIG:Debug>:-Wl,--wrap=sdl3_visual_render>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_SetRenderTarget>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_SetRenderClipRect>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_SetRenderDrawColor>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_RenderClear>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_RenderFillRect>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_RenderFillRects>
    $<$<CONFIG:Debug>:-Wl,--wrap=SDL_RenderTexture>)
target_link_libraries(main PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    android
    log
    m)
