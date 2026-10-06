# F20 control software — top-level build file.
# Build:   ./tool/build.sh [Debug|Release]     (configure + build + verify)
# Tests:   ctest --preset Debug
cmake_minimum_required(VERSION 3.22)
project(f20software LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets the IDE's analyzer see includes

# All programs land in one place; tool/build.sh verifies them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

enable_testing()

add_subdirectory(common)    # protocol library, no Qt
add_subdirectory(sim)       # f20bridge-sim
add_subdirectory(app)       # f20app (Qt 6) + its Qt-free core
add_subdirectory(tests)     # unit tests (doctest)

# Added in phase 6, built on Windows only:
# add_subdirectory(bridge)  # f20bridge, C++/CLI gateway
