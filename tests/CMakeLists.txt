# Host tests for the FX Pad, no board. Build and run:
#   cmake -S tests -B tests/build && cmake --build tests/build && ctest --test-dir tests/build
# crosspad_fxpad_tests: the sound -- the engine and its effects, and the
# screen's table of them (FxDef.hpp: axes, ranges, release); no LVGL.
# crosspad_fxpad_ui_tests: the screen (FxPadUi.cpp) on LVGL with a buffer for a
# display and a scripted finger: nothing on any tab is cut by what it sits in.
# crosspad-core and crosspad-gui are used for their headers; by default they
# are the sibling components of a platform checkout, else pass
# -DCROSSPAD_CORE_DIR=<path> -DCROSSPAD_GUI_DIR=<path>. LVGL is the platform
# checkout's managed component (the board's version), else -DLVGL_DIR=<path>,
# else it is fetched.

cmake_minimum_required(VERSION 3.14)
project(crosspad-fxpad-tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CROSSPAD_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../crosspad-core"
    CACHE PATH "crosspad-core checkout (headers only)")
set(CROSSPAD_GUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../crosspad-gui"
    CACHE PATH "crosspad-gui checkout (headers only)")
set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../managed_components/lvgl__lvgl"
    CACHE PATH "LVGL source tree (the board builds 9.3.0)")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FxDefs.cpp
)
target_include_directories(crosspad_fxpad_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CROSSPAD_CORE_DIR}/include
)
target_compile_options(crosspad_fxpad_tests PRIVATE -Wall -Wextra -O2)
target_link_libraries(crosspad_fxpad_tests PRIVATE Catch2::Catch2WithMain)
catch_discover_tests(crosspad_fxpad_tests)

# ---- The screen ----
if(NOT EXISTS "${LVGL_DIR}/src/lv_init.c")
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG        v9.3.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  none          # its sources only: built below with tests/lv_conf.h
    )
    FetchContent_MakeAvailable(lvgl)
    set(LVGL_DIR "${lvgl_SOURCE_DIR}" CACHE PATH "LVGL source tree (the board builds 9.3.0)" FORCE)
endif()
file(GLOB_RECURSE _lvgl_srcs "${LVGL_DIR}/src/*.c")
add_library(fxpad_test_lvgl STATIC ${_lvgl_srcs})
target_compile_definitions(fxpad_test_lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(fxpad_test_lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${LVGL_DIR} ${LVGL_DIR}/src)
target_compile_options(fxpad_test_lvgl PRIVATE -O1 -w)

add_executable(crosspad_fxpad_ui_tests
    test_fx_ui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FxPadUi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FxEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/FxDefs.cpp
)
target_compile_definitions(crosspad_fxpad_ui_tests PRIVATE USE_LVGL)
target_include_directories(crosspad_fxpad_ui_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CROSSPAD_CORE_DIR}/include
    ${CROSSPAD_GUI_DIR}/include
)
target_compile_options(crosspad_fxpad_ui_tests PRIVATE -Wall -Wextra -O2)
target_link_libraries(crosspad_fxpad_ui_tests PRIVATE fxpad_test_lvgl Catch2::Catch2WithMain)
catch_discover_tests(crosspad_fxpad_ui_tests)
