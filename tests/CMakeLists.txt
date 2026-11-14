# Host tests for the FX Pad's sound: the engine and its effects alone, no
# LVGL, no board. Build and run:
#   cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build
# crosspad-core is used for its headers; by default it is the sibling
# component of a platform checkout, else pass -DCROSSPAD_CORE_DIR=<path>.

cmake_minimum_required(VERSION 3.14)
project(crosspad-fxpad-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CROSSPAD_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../crosspad-core"
    CACHE PATH "crosspad-core checkout (headers only)")

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(Catch2)

include(CTest)
include(Catch)

add_executable(crosspad_fxpad_tests
    test_fx_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FxEngine.cpp
)
target_include_directories(crosspad_fxpad_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CROSSPAD_CORE_DIR}/include
)
target_compile_options(crosspad_fxpad_tests PRIVATE -Wall -Wextra -O2)
target_link_libraries(crosspad_fxpad_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(crosspad_fxpad_tests)
