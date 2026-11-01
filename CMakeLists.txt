cmake_minimum_required(VERSION 3.20)
project(hge_sdl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# CMake 4 removed compatibility with cmake_minimum_required(<3.5) while SDL2
# still declares 3.0. This escape hatch keeps the official SDL2 source buildable.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()

# ---- SDL2 ---------------------------------------------------------------
# Built from source so Windows and Linux (including cross compiles for ARM)
# share one dependency and one version. Override with -DHGE_SDL2_SOURCE=<dir>.
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

# ---- M1 skeleton --------------------------------------------------------
add_executable(m1_demo
    src/app/m1_demo.cpp
    src/platform/plat_sdl2.cpp
    src/gfx/gl_api.cpp
    src/gfx/gfx_gl.cpp
)
target_include_directories(m1_demo PRIVATE include src)
target_link_libraries(m1_demo PRIVATE SDL2::SDL2-static)
target_compile_definitions(m1_demo PRIVATE SDL_MAIN_HANDLED)

if(MSVC)
    target_compile_options(m1_demo PRIVATE /utf-8 /W3)
else()
    target_compile_options(m1_demo PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Embedded / ES-only profile: -DHGE_GLES=ON requests an OpenGL ES 2.0 context.
option(HGE_GLES "Build for OpenGL ES 2.0 (embedded Linux, KMSDRM)" OFF)
if(HGE_GLES)
    target_compile_definitions(m1_demo PRIVATE HGE_GLES=1)
endif()

# ---- Bubble Level 1 -----------------------------------------------------
add_executable(bubble_level1 WIN32
    src/app/bubble/main.cpp
    src/app/bubble/assets.cpp
    src/app/bubble/game.cpp
    src/app/bubble/audio.cpp
    src/platform/plat_sdl2.cpp
    src/gfx/gl_api.cpp
    src/gfx/gfx_gl.cpp
)
target_include_directories(bubble_level1 PRIVATE include src)
target_link_libraries(bubble_level1 PRIVATE SDL2::SDL2-static)
target_compile_definitions(bubble_level1 PRIVATE SDL_MAIN_HANDLED)
if(HGE_GLES)
    target_compile_definitions(bubble_level1 PRIVATE HGE_GLES=1)
endif()
if(MSVC)
    target_compile_options(bubble_level1 PRIVATE /utf-8 /W3)
    # WIN32 subsystem + our own main(): use the CRT entry instead of WinMain
    target_link_options(bubble_level1 PRIVATE "/ENTRY:mainCRTStartup")
else()
    target_compile_options(bubble_level1 PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Embed the asset packs into the exe so it ships as a single file.
# (The exe still falls back to game.pak / audio.pak next to it if present.)
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
