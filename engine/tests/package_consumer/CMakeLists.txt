cmake_minimum_required(VERSION 3.20)
project(hge_consumer CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The whole point of this project: consume the engine ONLY as an installed
# package, exactly like an out-of-tree game would.
find_package(hge 0.1 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE hge::hge)
