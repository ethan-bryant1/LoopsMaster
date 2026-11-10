cmake_minimum_required(VERSION 3.25.0)

# On Mac: one universal binary for Apple silicon and Intel, macOS 10.13 and newer. Ignored elsewhere.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(LoopsMaster VERSION 1.7.0)

set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/vst3sdk")

# Only the plugin, the SDK, the window toolkit and the validator are needed.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "" FORCE)
# The default link into Program Files needs admin rights; the install step is separate.
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(LoopsMaster
    source/dsp_core.h
    source/dsp.h
    source/master.h
    source/editor.h
    source/editor.cpp
    source/logo_png.h
    source/loopsmaster.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
)
target_compile_features(LoopsMaster PUBLIC cxx_std_17)
target_link_libraries(LoopsMaster PRIVATE sdk vstgui_support)

# Audio test: exercises the engine directly, without a host.
add_executable(dsp_test tools/dsp_test.cpp)
target_include_directories(dsp_test PRIVATE source)
target_compile_features(dsp_test PUBLIC cxx_std_17)

# The tools below host the plugin through Windows-only code, so they are built on Windows only.
if(WIN32)
# Window test: loads the built plugin, plays audio through it, uses the controls, saves pictures.
add_executable(ui_test WIN32 tools/ui_test.cpp ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
target_compile_features(ui_test PUBLIC cxx_std_17)
target_link_libraries(ui_test PRIVATE sdk_hosting)

# Control audit: renders audio through the built plugin with each control moved.
add_executable(param_audit tools/param_audit.cpp ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
target_compile_features(param_audit PUBLIC cxx_std_17)
target_link_libraries(param_audit PRIVATE sdk_hosting)

# Lists what a plugin publishes to hosts: buses, parameters, latency.
add_executable(plugin_probe tools/plugin_probe.cpp ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
target_compile_features(plugin_probe PUBLIC cxx_std_17)
target_link_libraries(plugin_probe PRIVATE sdk_hosting)


# Black-box measurements of a plugin's audio behaviour.
add_executable(plugin_measure tools/plugin_measure.cpp ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
target_compile_features(plugin_measure PUBLIC cxx_std_17)
target_link_libraries(plugin_measure PRIVATE sdk_hosting)
endif()

