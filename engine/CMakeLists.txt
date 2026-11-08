# ===========================================================================
# hge — the engine build.
#
# This file must work in two roles:
#   1. game repo : root CMakeLists.txt -> add_subdirectory(engine)
#   2. engine repo (hge-ember): this file IS the top-level CMakeLists.txt
#
# Build axes (orthogonal):
#   HGE_PLATFORM : desktop | miyoo      (OS / device specifics)
#   HGE_GFX      : gl | sdl | blit      (renderer backend)
#   HGE_HOST     : standalone | libretro (who drives the frame loop)
# ===========================================================================

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(hge LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    endif()
    set(HGE_STANDALONE_REPO ON)
else()
    set(HGE_STANDALONE_REPO OFF)
endif()

# Shared resources (third_party/, libs/) sit beside the engine in a game repo
# and inside the engine repo when it stands alone.
if(HGE_STANDALONE_REPO)
    set(HGE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
else()
    get_filename_component(HGE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

include(GNUInstallDirs)

set(HGE_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
set(HGE_LIBRETRO_INCLUDE_DIR "${HGE_REPO_ROOT}/third_party/libretro" PARENT_SCOPE)
set(HGE_VERSION "0.1.0" CACHE STRING "Engine version reported to consumers")

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
option(HGE_DEBUG_ATTACH "Allow gdb to attach without an env var (Linux, dev builds)" OFF)
option(HGE_INSTALL "Generate install/export rules so consumers can find_package(hge)" ON)

set(HGE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/hge/hge_config.h.in"
    "${HGE_GENERATED_DIR}/hge_config.h"
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
if(HGE_DEBUG_ATTACH AND HGE_PLATFORM STREQUAL "desktop")
    list(APPEND HGE_PUBLIC_DEFS HGE_DEBUG_ATTACH=1)
endif()

# ---------------------------------------------------------------------------
# SDL2: built from source on desktop; reused from the Miyoo prebuilt on device.
# ---------------------------------------------------------------------------
if(HGE_PLATFORM STREQUAL "miyoo")
    add_library(hge_sdl2 SHARED IMPORTED)
    set_target_properties(hge_sdl2 PROPERTIES
        IMPORTED_LOCATION "${HGE_REPO_ROOT}/libs/libSDL2-2.0.so.0"
        INTERFACE_INCLUDE_DIRECTORIES "${HGE_REPO_ROOT}/third_party/SDL2/include")
    set(HGE_SDL_TARGET hge_sdl2)
else()
    set(HGE_SDL2_SOURCE "" CACHE PATH "SDL2 source tree containing CMakeLists.txt")
    if(NOT HGE_SDL2_SOURCE)
        foreach(candidate
                "${HGE_REPO_ROOT}/third_party/SDL2"
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
    # Keep SDL2's own install rules so "cmake --install" yields a prefix where
    # find_package(hge) can resolve SDL2::SDL2-static.
    if(HGE_INSTALL)
        set(SDL2_DISABLE_INSTALL OFF CACHE BOOL "" FORCE)
    else()
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory("${HGE_SDL2_SOURCE}" "${CMAKE_BINARY_DIR}/sdl2" EXCLUDE_FROM_ALL)
    set(HGE_SDL_TARGET SDL2::SDL2-static)
endif()

# ---------------------------------------------------------------------------
# Engine static library
# ---------------------------------------------------------------------------
set(HGE_CORE_SRC
    src/core/app.cpp
    src/core/gfx_factory.cpp
    src/core/system.cpp)

set(HGE_SERVICE_SRC
    src/audio/audio.cpp
    src/resource/resource.cpp)

if(HGE_GFX STREQUAL "gl")
    set(HGE_GFX_SRC src/gfx/gfx_gl.cpp src/gfx/gl_api.cpp)
elseif(HGE_GFX STREQUAL "sdl")
    set(HGE_GFX_SRC src/gfx/gfx_sdl.cpp)
else()
    set(HGE_GFX_SRC src/gfx/gfx_blit.cpp)
endif()

if(HGE_HOST STREQUAL "libretro")
    set(HGE_PLATFORM_SRC src/platform/plat_libretro.cpp)
elseif(HGE_PLATFORM STREQUAL "miyoo")
    set(HGE_PLATFORM_SRC
        src/platform/plat_sdl2.cpp
        src/platform/plat_sdl2_miyoo.cpp)
else()
    set(HGE_PLATFORM_SRC
        src/platform/plat_sdl2.cpp
        src/platform/plat_sdl2_desktop.cpp)
endif()

add_library(hge STATIC
    ${HGE_CORE_SRC}
    ${HGE_SERVICE_SRC}
    ${HGE_GFX_SRC}
    ${HGE_PLATFORM_SRC})

# Public headers are flat ("#include <hge_gfx.h>") and get installed to
# <prefix>/include/hge, so the install interface repeats that subdirectory.
target_include_directories(hge PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/hge>
    $<BUILD_INTERFACE:${HGE_GENERATED_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/hge>)
target_include_directories(hge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(hge PUBLIC ${HGE_PUBLIC_DEFS})
# BUILD_INTERFACE keeps the in-tree SDL2 target private to this build;
# INSTALL_INTERFACE names it again so the installed package resolves it through
# find_dependency(SDL2) instead of pointing at our build tree.
target_link_libraries(hge PUBLIC
    $<BUILD_INTERFACE:${HGE_SDL_TARGET}>
    $<INSTALL_INTERFACE:SDL2::SDL2-static>)

find_package(Threads REQUIRED)
target_link_libraries(hge PUBLIC Threads::Threads)
if(NOT WIN32)
    target_link_libraries(hge PUBLIC ${CMAKE_DL_LIBS} m)
endif()

if(HGE_PLATFORM STREQUAL "miyoo" AND HGE_HOST STREQUAL "standalone")
    target_link_libraries(hge PUBLIC mi_sys mi_gfx mi_common)
endif()

if(HGE_PLATFORM STREQUAL "miyoo")
    target_link_options(hge INTERFACE
        -Wl,--allow-shlib-undefined
        -Wl,-rpath-link,${HGE_REPO_ROOT}/libs)
endif()

# ---------------------------------------------------------------------------
# Warnings helper (also used by games added after add_subdirectory(engine))
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
# Examples: hello_game is the include-boundary probe, m1_demo the raw-plat demo
# ---------------------------------------------------------------------------
if(HGE_HOST STREQUAL "standalone")
    add_executable(hello_game "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello_game/main.cpp")
    target_link_libraries(hello_game PRIVATE hge)
    hge_set_warnings(hello_game)

    if(HGE_GFX STREQUAL "gl")
        add_executable(m1_demo "${CMAKE_CURRENT_SOURCE_DIR}/examples/m1_demo/m1_demo.cpp")
        target_include_directories(m1_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(m1_demo PRIVATE hge)
        hge_set_warnings(m1_demo)
    endif()
endif()

# Renderer throughput probe. Engine-internal: it drives plat:: directly so each
# stage (submit+rasterize vs present) can be timed without frame pacing hiding
# the cost. Built for any standalone target, desktop or device.
if(HGE_HOST STREQUAL "standalone")
    add_executable(quadbench "${CMAKE_CURRENT_SOURCE_DIR}/tools/quadbench/main.cpp")
    target_include_directories(quadbench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(quadbench PRIVATE hge)
    hge_set_warnings(quadbench)
    if(MINGW)
        target_link_options(quadbench PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---------------------------------------------------------------------------
# Engine self-tests (never cross-compiled)
# ---------------------------------------------------------------------------
if(HGE_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING AND HGE_INSTALL)
    set(HGE_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

    add_executable(gfx_conformance_blit
        "${HGE_TEST_DIR}/gfx_conformance_blit.cpp"
        "${HGE_TEST_DIR}/plat_stub.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/gfx_blit.cpp")
    target_include_directories(gfx_conformance_blit PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hge"
        "${HGE_GENERATED_DIR}")
    target_link_libraries(gfx_conformance_blit PRIVATE Threads::Threads)
    hge_set_warnings(gfx_conformance_blit)
    add_test(NAME gfx_conformance_blit COMMAND gfx_conformance_blit)

    add_executable(gfx_conformance_sdl
        "${HGE_TEST_DIR}/gfx_conformance_sdl.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/gfx_sdl.cpp")
    target_include_directories(gfx_conformance_sdl PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/hge")
    target_link_libraries(gfx_conformance_sdl PRIVATE ${HGE_SDL_TARGET} Threads::Threads)
    if(NOT WIN32)
        target_link_libraries(gfx_conformance_sdl PRIVATE ${CMAKE_DL_LIBS} m)
    endif()
    hge_set_warnings(gfx_conformance_sdl)
    add_test(NAME gfx_conformance_sdl COMMAND gfx_conformance_sdl)
endif()

# ---------------------------------------------------------------------------
# Package consumer probe: install into a throwaway prefix, then build an
# out-of-tree project that only does find_package(hge). Skipped when no SDL2
# CMake package is available, because hgeConfig resolves SDL2::SDL2-static
# through find_dependency(SDL2).
# ---------------------------------------------------------------------------
if(HGE_BUILD_TESTS AND HGE_INSTALL AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32
   AND NOT HGE_PLATFORM STREQUAL "miyoo")
    find_file(HGE_SDL2_PACKAGE_CONFIG
        NAMES SDL2Config.cmake sdl2-config.cmake
        PATH_SUFFIXES cmake/SDL2
        PATHS /usr/lib /usr/lib64 /usr/local/lib
              /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE})
    if(HGE_SDL2_PACKAGE_CONFIG)
        add_test(NAME hge_package_consumer
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_package_consumer.sh"
                    "${CMAKE_BINARY_DIR}"
                    "${CMAKE_BINARY_DIR}/package-test-prefix"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/package_consumer")
    else()
        message(STATUS "hge_package_consumer test skipped (no SDL2 CMake package)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Install / export so a consumer can use the engine as a package
# ---------------------------------------------------------------------------
if(HGE_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS hge EXPORT hgeTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/hge/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hge"
        FILES_MATCHING PATTERN "*.h")
    install(FILES "${HGE_GENERATED_DIR}/hge_config.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/hge")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/hgeConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/hgeConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hge")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/hgeConfigVersion.cmake"
        VERSION "${HGE_VERSION}"
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/hgeConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/hgeConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hge")
    install(EXPORT hgeTargets
        NAMESPACE hge::
        FILE hgeTargets.cmake
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/hge")
endif()
