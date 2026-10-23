cmake_minimum_required(VERSION 3.21)
# macOS: as StageHand's (its docs/macos.md, decisions 9 and 13). Set before
# project(), and ignored elsewhere. The macOS port comes later (decisions.md).
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "macOS 26 or later, as StageHand")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "arm64 only, as StageHand")
project(UmpireDAW VERSION 0.0.1 LANGUAGES C CXX)   # C: DGL's pugl and NanoVG

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# --- uw-core (git submodule, external/uw-core): the code Umpire shares with StageHand.
# Its backends: MIDI input since milestone 5 (ALSA sequencer UMP on Linux,
# CoreMIDI on macOS); audio (JACK, CoreAudio) is built with them, used from 8.
set(UW_CORE_BACKENDS ON CACHE BOOL "" FORCE)
set(UW_CORE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/uw-core)

# --- DPF's DGL (pugl + NanoVG over OpenGL): the private DPF fork, a git submodule
# (external/dpf, its README.fork.md lists the patches; decisions.md, "DPF fork").
# Only the DGL library, no plugin wrappers.
if(APPLE)
    enable_language(OBJCXX)   # pugl's Cocoa backend
endif()
set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/dpf EXCLUDE_FROM_ALL)
dpf__add_dgl_opengl(TRUE FALSE FALSE)   # shared resources (DejaVu Sans); no file browser, no web view
# Umpire's own namespace: on macOS pugl names its Objective-C classes after it,
# and hosted DPF plugins register their own. And vsync off (the fork's opt-in
# patch; docs/ui-spike.md, finding 6): Umpire draws on demand anyway.
target_compile_definitions(dgl-opengl PUBLIC DGL_NAMESPACE=UmpireDGL DGL_SWAP_INTERVAL=0)
foreach(dgl_target dgl-opengl dgl-opengl-definitions)
    get_target_property(dgl_includes ${dgl_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(dgl_includes)
        set_target_properties(${dgl_target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dgl_includes}")
    endif()
endforeach()
if(APPLE)
    target_compile_options(dgl-opengl PRIVATE -Wno-deprecated-declarations)   # pugl's pre-10.12 AppKit names
else()
    target_compile_options(dgl-opengl PRIVATE -Wno-cpp)   # pugl.hpp's <cstdbool> under GCC 15
endif()

# --- Dear ImGui 1.91.9b: the sources DPF-Widgets carries (git submodule,
# external/DPF-Widgets), with DPF's patch to them. Umpire's own widget
# (src/Ui/ImGuiWidget) replaces DPF-Widgets' (docs/ui-spike.md, proposal 2).
set(imgui_dir external/DPF-Widgets/opengl/DearImGui)
add_library(umpire-imgui STATIC
    ${imgui_dir}/imgui.cpp
    ${imgui_dir}/imgui_demo.cpp
    ${imgui_dir}/imgui_draw.cpp
    ${imgui_dir}/imgui_tables.cpp
    ${imgui_dir}/imgui_widgets.cpp
    ${imgui_dir}/imgui_impl_opengl2.cpp)
target_include_directories(umpire-imgui SYSTEM PUBLIC ${imgui_dir})
target_compile_definitions(umpire-imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS PRIVATE IMGUI_DPF_BACKEND)
target_link_libraries(umpire-imgui PUBLIC OpenGL::GL)
target_compile_options(umpire-imgui PRIVATE -w)   # third-party
if(APPLE)
    target_compile_definitions(umpire-imgui PRIVATE GL_SILENCE_DEPRECATION)
endif()

# --- Warnings for our own code -----------------------------------------------
add_library(umpire-warnings INTERFACE)
target_compile_options(umpire-warnings INTERFACE -Wall -Wextra -Wpedantic)

# --- umpire-app: the tracks, their controller and view models, MIDI input, and
# the main loop. No DGL or ImGui: their headers aren't on its include path
# (decisions.md: only the drawing layer sees the toolkit). It sees uw-core's
# MidiInput seam, not a platform's: main.cpp makes that.
add_library(umpire-app STATIC
    src/App/Clip.cpp
    src/App/GridModel.cpp
    src/App/MidiDescribe.cpp
    src/App/MidiInputs.cpp
    src/App/TrackController.cpp)
if(APPLE)
    target_sources(umpire-app PRIVATE src/App/MainLoopMac.mm)
    set_source_files_properties(src/App/MainLoopMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(umpire-app PUBLIC "-framework AppKit")
else()
    target_sources(umpire-app PRIVATE src/App/MainLoop.cpp)
endif()
target_include_directories(umpire-app PUBLIC src)
target_link_libraries(umpire-app PUBLIC uw::base uw::plugin PRIVATE umpire-warnings)

# --- umpire-ui: the main window (DGL, NanoVG, ImGui). Only this library sees the toolkit.
add_library(umpire-ui STATIC
    src/Ui/GridCanvas.cpp
    src/Ui/GridPanels.cpp
    src/Ui/GridSelfTest.cpp
    src/Ui/ImGuiWidget.cpp
    src/Ui/MainWindow.cpp
    src/Ui/TrackCanvas.cpp
    src/Ui/TrackPanels.cpp
    src/Ui/TrackSelfTest.cpp
    src/Ui/Widgets.cpp)
target_link_libraries(umpire-ui
    PUBLIC  umpire-app
    PRIVATE dgl-opengl dgl-opengl-definitions umpire-imgui umpire-warnings)
if(APPLE)
    target_compile_definitions(umpire-ui PRIVATE GL_SILENCE_DEPRECATION)
endif()

# --- umpire-daw: the app ---------------------------------------------------
add_executable(umpire-daw src/App/main.cpp)
target_link_libraries(umpire-daw PRIVATE umpire-ui umpire-app uw::midi umpire-warnings)

# --- Tests (silent; one executable per suite) ---------------------------------
# The window's own check, `umpire-daw --self-test DIR`, opens a window, so it
# isn't one of these (docs/skeleton.md).
enable_testing()
function(umpire_add_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_link_libraries(${NAME} PRIVATE umpire-app umpire-warnings)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()
umpire_add_test(test-grid-model       tests/GridModelTests.cpp)
umpire_add_test(test-track-controller tests/TrackControllerTests.cpp)
