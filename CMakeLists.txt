cmake_minimum_required(VERSION 3.20)
project(hge_sdl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# CMake 4 removed compatibility with cmake_minimum_required(<3.5) while SDL2
# still declares 3.0. This escape hatch keeps the official SDL2 source buildable.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Build axes (orthogonal):
#   HGE_PLATFORM : desktop | miyoo   (OS / device specifics)
#   HGE_GFX      : gl | sdl | blit  (renderer backend)
#   HGE_HOST     : standalone | libretro (who drives the frame loop)
# ---------------------------------------------------------------------------
set(HGE_PLATFORM "desktop" CACHE STRING "Platform backend: desktop|miyoo")
set_property(CACHE HGE_PLATFORM PROPERTY STRINGS desktop miyoo)
set(HGE_GFX "gl" CACHE STRING "Graphics backend: gl|sdl|blit")
set_property(CACHE HGE_GFX PROPERTY STRINGS gl sdl blit)
set(HGE_HOST "standalone" CACHE STRING "Host form: standalone|libretro")
set_property(CACHE HGE_HOST PROPERTY STRINGS standalone libretro)
option(HGE_GLES "Build the desktop backend against OpenGL ES 2.0 (kmsdrm / embedded)" OFF)
option(HGE_FEATURE_AUDIO "Build the audio mixer" ON)
option(HGE_FEATURE_THREADS "Allow optional threads (blitter worker)" ON)
option(HGE_FEATURE_FILESYSTEM "Allow filesystem-backed resources" ON)
option(HGE_BUILD_TESTS "Build host-side test executables" ON)

set(HGE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/hge/hge_config.h.in"
    "${HGE_GENERATED_DIR}/hge/hge_config.h"
    @ONLY)

if(NOT HGE_PLATFORM MATCHES "^(desktop|miyoo)$")
    message(FATAL_ERROR "HGE_PLATFORM must be desktop or miyoo")
endif()
if(NOT HGE_GFX MATCHES "^(gl|sdl|blit)$")
    message(FATAL_ERROR "HGE_GFX must be gl, sdl or blit")
endif()
if(NOT HGE_HOST MATCHES "^(standalone|libretro)$")
    message(FATAL_ERROR "HGE_HOST must be standalone or libretro")
endif()
if(HGE_HOST STREQUAL "libretro" AND NOT HGE_GFX STREQUAL "blit")
    message(FATAL_ERROR "HGE_HOST=libretro currently requires HGE_GFX=blit")
endif()

set(HGE_PUBLIC_DEFS SDL_MAIN_HANDLED)
if(HGE_PLATFORM STREQUAL "miyoo")
    list(APPEND HGE_PUBLIC_DEFS HGE_PLATFORM_MIYOO=1)
endif()
if(HGE_HOST STREQUAL "libretro")
    list(APPEND HGE_PUBLIC_DEFS HGE_LIBRETRO=1)
endif()
if(HGE_GFX STREQUAL "gl")
    list(APPEND HGE_PUBLIC_DEFS HGE_GFX_GL=1)
elseif(HGE_GFX STREQUAL "sdl")
    list(APPEND HGE_PUBLIC_DEFS HGE_GFX_SDL=1)
else()
    list(APPEND HGE_PUBLIC_DEFS HGE_GFX_BLIT=1)
endif()
if(HGE_GLES)
    list(APPEND HGE_PUBLIC_DEFS HGE_GLES=1)
endif()

# ---------------------------------------------------------------------------
# SDL2: built from source on desktop; reused from the Miyoo prebuilt on device.
# ---------------------------------------------------------------------------
if(HGE_PLATFORM STREQUAL "miyoo")
    add_library(hge_sdl2 SHARED IMPORTED)
    set_target_properties(hge_sdl2 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/libSDL2-2.0.so.0"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/include")
    set(HGE_SDL_TARGET hge_sdl2)
else()
    set(HGE_SDL2_SOURCE "" CACHE PATH "SDL2 source tree containing CMakeLists.txt")
    if(NOT HGE_SDL2_SOURCE)
        foreach(candidate
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2"
                "G:/HGE_Dev/third_party/SDL2")
            if(EXISTS "${candidate}/CMakeLists.txt")
                set(HGE_SDL2_SOURCE "${candidate}")
                break()
            endif()
        endforeach()
    endif()
    if(NOT HGE_SDL2_SOURCE OR NOT EXISTS "${HGE_SDL2_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "SDL2 source not found.\n"
            "Download https://www.libsdl.org/release/SDL2-2.32.10.tar.gz and extract it,\n"
            "then configure with -DHGE_SDL2_SOURCE=<path>.")
    endif()
    message(STATUS "SDL2 source: ${HGE_SDL2_SOURCE}")

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${HGE_SDL2_SOURCE}" "${CMAKE_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)
    set(HGE_SDL_TARGET SDL2::SDL2-static)
endif()

# ---------------------------------------------------------------------------
# Engine static library
# ---------------------------------------------------------------------------
set(HGE_CORE_SRC
    engine/src/core/app.cpp
    engine/src/core/gfx_factory.cpp
    engine/src/core/system.cpp)

set(HGE_SERVICE_SRC
    engine/src/audio/audio.cpp
    engine/src/resource/resource.cpp)

if(HGE_GFX STREQUAL "gl")
    set(HGE_GFX_SRC engine/src/gfx/gfx_gl.cpp engine/src/gfx/gl_api.cpp)
elseif(HGE_GFX STREQUAL "sdl")
    set(HGE_GFX_SRC engine/src/gfx/gfx_sdl.cpp)
else()
    set(HGE_GFX_SRC engine/src/gfx/gfx_blit.cpp)
endif()

if(HGE_HOST STREQUAL "libretro")
    set(HGE_PLATFORM_SRC engine/src/platform/plat_libretro.cpp)
elseif(HGE_PLATFORM STREQUAL "miyoo")
    set(HGE_PLATFORM_SRC
        engine/src/platform/plat_sdl2.cpp
        engine/src/platform/plat_sdl2_miyoo.cpp)
else()
    set(HGE_PLATFORM_SRC
        engine/src/platform/plat_sdl2.cpp
        engine/src/platform/plat_sdl2_desktop.cpp)
endif()

add_library(hge STATIC
    ${HGE_CORE_SRC}
    ${HGE_SERVICE_SRC}
    ${HGE_GFX_SRC}
    ${HGE_PLATFORM_SRC})

target_include_directories(hge PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/hge"
    "${HGE_GENERATED_DIR}")
target_include_directories(hge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src")
target_compile_definitions(hge PUBLIC ${HGE_PUBLIC_DEFS})
target_link_libraries(hge PUBLIC ${HGE_SDL_TARGET})

find_package(Threads REQUIRED)
target_link_libraries(hge PUBLIC Threads::Threads)
if(NOT WIN32)
    target_link_libraries(hge PUBLIC ${CMAKE_DL_LIBS} m)
endif()

if(HGE_PLATFORM STREQUAL "miyoo" AND HGE_HOST STREQUAL "standalone")
    target_link_libraries(hge PUBLIC mi_sys mi_gfx mi_common)
endif()

if(HGE_PLATFORM STREQUAL "miyoo")
    set(HGE_ARM_LINK_OPTIONS
        -Wl,--allow-shlib-undefined
        -Wl,-rpath-link,${CMAKE_CURRENT_SOURCE_DIR}/libs)
else()
    set(HGE_ARM_LINK_OPTIONS)
endif()

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------
function(hge_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

hge_set_warnings(hge)

# ---------------------------------------------------------------------------
# Bubble game
# ---------------------------------------------------------------------------
set(BUBBLE_COMMON_SRC
    games/bubble/src/app/bubble/battle.cpp
    games/bubble/src/app/bubble/bubble_app.cpp
    games/bubble/src/app/bubble/game.cpp
    games/bubble/src/app/bubble/render.cpp)
set(BUBBLE_MAIN_SRC
    games/bubble/src/app/bubble/main.cpp)

if(HGE_HOST STREQUAL "standalone")
    if(WIN32)
        add_executable(bubble_level1 WIN32 ${BUBBLE_COMMON_SRC} ${BUBBLE_MAIN_SRC})
    else()
        add_executable(bubble_level1 ${BUBBLE_COMMON_SRC} ${BUBBLE_MAIN_SRC})
    endif()
    target_include_directories(bubble_level1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/games/bubble/src")
    target_link_libraries(bubble_level1 PRIVATE hge)
    target_link_options(bubble_level1 PRIVATE ${HGE_ARM_LINK_OPTIONS})
    hge_set_warnings(bubble_level1)

    if(MSVC)
        # WIN32 subsystem + our own main(): use the CRT entry instead of WinMain
        target_link_options(bubble_level1 PRIVATE "/ENTRY:mainCRTStartup")
    elseif(MINGW)
        target_link_options(bubble_level1 PRIVATE -Wl,-e,mainCRTStartup)
        target_link_options(bubble_level1 PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    # Embed the asset packs into the desktop exe so it ships as a single file.
    if(HGE_PLATFORM STREQUAL "desktop")
        find_program(HGE_PYTHON NAMES python python3)
        if(HGE_PYTHON)
            add_custom_command(TARGET bubble_level1 POST_BUILD
                COMMAND "${HGE_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_exe.py"
                        "$<TARGET_FILE:bubble_level1>"
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/game.pak"
                        "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio.pak"
                COMMENT "Embedding game.pak + audio.pak into bubble_level1")
        else()
            message(WARNING "python not found - bubble_level1 will need game.pak/audio.pak beside it")
        endif()
    endif()
else()
    add_library(bubble_libretro MODULE
        ${BUBBLE_COMMON_SRC}
        games/bubble/src/app/bubble/bubble_libretro.cpp
        engine/src/host/libretro_core.cpp)
    target_include_directories(bubble_libretro PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/games/bubble/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libretro")
    target_link_libraries(bubble_libretro PRIVATE hge)
    target_link_options(bubble_libretro PRIVATE ${HGE_ARM_LINK_OPTIONS})
    set_target_properties(bubble_libretro PROPERTIES PREFIX "")
    hge_set_warnings(bubble_libretro)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(HGE_HOST STREQUAL "standalone" AND HGE_GFX STREQUAL "gl")
    add_executable(m1_demo examples/m1_demo/m1_demo.cpp)
    target_include_directories(m1_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/engine/src")
    target_link_libraries(m1_demo PRIVATE hge)
    hge_set_warnings(m1_demo)
endif()

if(HGE_HOST STREQUAL "standalone")
    add_executable(hello_game examples/hello_game/main.cpp)
    target_link_libraries(hello_game PRIVATE hge)
    target_link_options(hello_game PRIVATE ${HGE_ARM_LINK_OPTIONS})
    hge_set_warnings(hello_game)
endif()

# ---------------------------------------------------------------------------
# Host-side tests (not built when cross-compiling)
# ---------------------------------------------------------------------------
if(HGE_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()

    add_executable(gfx_conformance_blit
        tests/gfx_conformance_blit.cpp
        tests/plat_stub.cpp
        engine/src/gfx/gfx_blit.cpp)
    target_include_directories(gfx_conformance_blit PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/hge"
        "${HGE_GENERATED_DIR}")
    target_link_libraries(gfx_conformance_blit PRIVATE Threads::Threads)
    hge_set_warnings(gfx_conformance_blit)
    add_test(NAME gfx_conformance_blit COMMAND gfx_conformance_blit)

    add_executable(gfx_conformance_sdl
        tests/gfx_conformance_sdl.cpp
        engine/src/gfx/gfx_sdl.cpp)
    target_include_directories(gfx_conformance_sdl PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/hge")
    target_link_libraries(gfx_conformance_sdl PRIVATE ${HGE_SDL_TARGET} Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(gfx_conformance_sdl PRIVATE ${CMAKE_DL_LIBS} m)
    endif()
    hge_set_warnings(gfx_conformance_sdl)
    add_test(NAME gfx_conformance_sdl COMMAND gfx_conformance_sdl)

    if(UNIX AND TARGET bubble_level1)
        add_test(NAME bubble_sim_golden
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_host_tests.sh"
                    "$<TARGET_FILE:bubble_level1>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/sim_5.txt")
    endif()
endif()
