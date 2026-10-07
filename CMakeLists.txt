cmake_minimum_required(VERSION 3.21)

project(UmpireDAW VERSION 0.0.1 LANGUAGES C CXX)   # C: DGL's pugl and NanoVG

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- DPF's DGL (pugl + NanoVG over OpenGL) -------------------------------------
# Milestone 1 builds against StageHand's vendored copy, so the spike tests the
# DGL both apps will share (docs/ui-spike.md). Where the shared code ends up
# isn't decided yet (docs/decisions.md); until then, point this at a StageHand
# checkout's external/dpf.
find_path(UMPIRE_DPF_DIR
    NAMES dgl/Window.hpp
    PATHS
        "${CMAKE_SOURCE_DIR}/../StageHand/external/dpf"
        "$ENV{HOME}/Dev-MIDI/StageHand/external/dpf"
        "$ENV{HOME}/dev/dev-audio/StageHand/external/dpf"
    NO_DEFAULT_PATH
    DOC "DPF root (StageHand's external/dpf)")
if(NOT UMPIRE_DPF_DIR)
    message(FATAL_ERROR "DPF not found: pass -DUMPIRE_DPF_DIR=<StageHand>/external/dpf")
endif()
message(STATUS "DPF: ${UMPIRE_DPF_DIR}")

set(DPF_LIBRARIES OFF CACHE BOOL "" FORCE)
set(DPF_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${UMPIRE_DPF_DIR}" "${CMAKE_BINARY_DIR}/dpf" EXCLUDE_FROM_ALL)
dpf__add_dgl_opengl(TRUE FALSE FALSE)   # shared resources (the font); no file browser, no web view
# Umpire's own namespace, as StageHand has its own: on macOS pugl names its
# Objective-C classes after it, and hosted DPF plugins register the default ones.
target_compile_definitions(dgl-opengl PUBLIC DGL_NAMESPACE=UmpireDGL)
foreach(dgl_target dgl-opengl dgl-opengl-definitions)
    get_target_property(dgl_includes ${dgl_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(dgl_includes)
        set_target_properties(${dgl_target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dgl_includes}")
    endif()
endforeach()
target_compile_options(dgl-opengl PRIVATE -Wno-cpp)   # pugl.hpp's <cstdbool> under GCC 15

# --- Dear ImGui through DPF-Widgets (git submodule, external/DPF-Widgets) -------
# DearImGui.cpp compiles ImGui itself (it #includes imgui*.cpp) plus the DGL widget.
add_library(umpire-imgui STATIC external/DPF-Widgets/opengl/DearImGui.cpp)
target_include_directories(umpire-imgui SYSTEM PUBLIC external/DPF-Widgets/opengl)
target_link_libraries(umpire-imgui PUBLIC dgl-opengl dgl-opengl-definitions)
target_compile_options(umpire-imgui PRIVATE -w)   # third-party

# --- Milestone 1: the DGL plus Dear ImGui spike --------------------------------
add_executable(umpire-ui-spike
    spike/main.cpp
    spike/NoteCanvas.cpp
    spike/ImGuiLayer.cpp
    spike/SelfTest.cpp
    spike/Vsync.cpp)
target_link_libraries(umpire-ui-spike PRIVATE umpire-imgui OpenGL::GL OpenGL::GLX)
target_compile_options(umpire-ui-spike PRIVATE -Wall -Wextra -Wpedantic)
