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

# Tests are enabled at the top level so ctest sees subdirectory tests too; each
# component still guards its own add_test with HGE_BUILD_TESTS.
enable_testing()

# Targets live in subdirectories now, but every existing script, preset and doc
# refers to build-native/bubble_level1, build-libretro/bubble_libretro.so and so
# on. Pin the output directories to the build root to keep those paths valid.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Games to build. Empty = engine only, which is exactly what the standalone
# engine repository (hge-ember) will look like after the split.
set(HGE_GAMES "bubble" CACHE STRING "Semicolon-separated list of games under games/ (empty = engine only)")

add_subdirectory(engine)

foreach(game IN LISTS HGE_GAMES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/games/${game}/CMakeLists.txt")
        add_subdirectory("games/${game}")
    else()
        message(WARNING "HGE_GAMES: no game '${game}' under games/")
    endif()
endforeach()
