# FX Pad -- ESP-IDF component.
#
# The one app of the fxpad firmware kit (crosspad-app.json "kits"): built into
# that image only. In any other kit it is a component with no sources, so the
# full firmware carries no FX Pad code -- every kB of an image is a kB of
# PSRAM (it is copied there at boot). The requirements stay the same either
# way: ESP-IDF reads them in an early pass that does not know the kit.
#
# The effects run once per audio block on the render thread: FxEngine.cpp is
# built -O2 over the image's -Os, as the mixer and the USB audio path are.

set(_fxpad_srcs "")
set(_fxpad_kit "full")
if(DEFINED CROSSPAD_FW_KIT)
    set(_fxpad_kit "${CROSSPAD_FW_KIT}")
endif()
file(READ "${CMAKE_CURRENT_LIST_DIR}/crosspad-app.json" _fxpad_app_json)
string(JSON _fxpad_kit_count LENGTH "${_fxpad_app_json}" kits)
math(EXPR _fxpad_last "${_fxpad_kit_count} - 1")
foreach(_i RANGE ${_fxpad_last})
    string(JSON _k GET "${_fxpad_app_json}" kits ${_i})
    if(_k STREQUAL _fxpad_kit)
        file(GLOB _fxpad_srcs "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")
    endif()
endforeach()

idf_component_register(
    SRCS ${_fxpad_srcs}
    INCLUDE_DIRS "src"
    REQUIRES
        crosspad-core
        crosspad-gui
        lvgl
)

if(_fxpad_srcs)
    set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/src/FxEngine.cpp" PROPERTIES COMPILE_OPTIONS -O2)
    # WHOLE_ARCHIVE keeps the REGISTER_APP-generated _register_FxPad_app()
    # alive through linking -- main calls it via AppRegistry_InitAll().
    set_property(TARGET ${COMPONENT_LIB} PROPERTY WHOLE_ARCHIVE ON)
endif()
