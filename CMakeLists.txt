cmake_minimum_required(VERSION 3.21)
# macOS: as StageHand's (its docs/macos.md, decisions 9 and 13). Set before
# project(), and ignored elsewhere.
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "macOS 26 or later, as StageHand")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "arm64 only, as StageHand")
project(UmpireDAW VERSION 0.9.1 LANGUAGES C CXX)   # C: DGL's pugl and NanoVG

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# The plugin (src/Plugin, milestone P1) links umpire-clip and uw-core into a
# shared module, so everything is position-independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

# --- umpire-clip: the clip model, MIDI Clip Files, editing a clip's notes and
# the grid's model, which need only uw::ump. Shared by the app and the plugin
# (docs/plugin-version.md, "What carries over").
add_library(umpire-clip STATIC
    src/App/Clip.cpp
    src/App/ClipEdit.cpp
    src/App/ClipFile.cpp
    src/App/GridModel.cpp
    src/App/Scale.cpp)
target_include_directories(umpire-clip PUBLIC src)
target_link_libraries(umpire-clip PUBLIC uw::ump PRIVATE umpire-warnings)
# Hidden, as uw-core's: linked into the plugin, it mustn't bind to another plugin's copy.
set_target_properties(umpire-clip PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# --- umpire-app: the tracks, their controller and view models, MIDI input, and
# the main loop. No DGL or ImGui: their headers aren't on its include path
# (decisions.md: only the drawing layer sees the toolkit). It sees uw-core's
# MidiInput seam, not a platform's: main.cpp makes that.
add_library(umpire-app STATIC
    src/App/AudioEngine.cpp
    src/App/Instruments.cpp
    src/App/MidiDescribe.cpp
    src/App/MidiInputs.cpp
    src/App/MidiOutputs.cpp
    src/App/Player.cpp
    src/App/SessionFile.cpp
    src/App/TrackController.cpp)
if(APPLE)
    target_sources(umpire-app PRIVATE src/App/MainLoopMac.mm src/App/FileDialogMac.mm)
    set_source_files_properties(src/App/MainLoopMac.mm src/App/FileDialogMac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(umpire-app PUBLIC "-framework AppKit" "-framework UniformTypeIdentifiers")
else()
    target_sources(umpire-app PRIVATE src/App/MainLoop.cpp src/App/FileDialog.cpp)
endif()
target_include_directories(umpire-app PUBLIC src)
# nlohmann/json 3.11.3, vendored (external/nlohmann/README.md): session.json only.
target_include_directories(umpire-app SYSTEM PRIVATE external)
target_link_libraries(umpire-app PUBLIC umpire-clip uw::base uw::plugin uw::clap-host uw::editor-window uw::audio PRIVATE umpire-warnings)
target_compile_definitions(umpire-app PRIVATE UMPIRE_VERSION_STRING="${PROJECT_VERSION}")   # the host version plugins see

# --- umpire-grid-ui: the grid (canvas and panels), Umpire's ImGui widget and the
# house widgets: what the app's grid windows and the plugin's window share
# (milestone P3). DGL, NanoVG and ImGui, over umpire-clip only.
add_library(umpire-grid-ui STATIC
    src/Ui/GridCanvas.cpp
    src/Ui/GridPanels.cpp
    src/Ui/ImGuiWidget.cpp
    src/Ui/Widgets.cpp)
target_include_directories(umpire-grid-ui PUBLIC src)
target_link_libraries(umpire-grid-ui
    PUBLIC  umpire-clip dgl-opengl dgl-opengl-definitions umpire-imgui
    PRIVATE umpire-warnings)
set_target_properties(umpire-grid-ui PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(APPLE)
    target_compile_definitions(umpire-grid-ui PRIVATE GL_SILENCE_DEPRECATION)
endif()

# --- umpire-ui: the main window (DGL, NanoVG, ImGui). Only this library and
# umpire-grid-ui see the toolkit.
add_library(umpire-ui STATIC
    src/Ui/GridSelfTest.cpp
    src/Ui/MainWindow.cpp
    src/Ui/TrackCanvas.cpp
    src/Ui/TrackPanels.cpp
    src/Ui/TrackSelfTest.cpp)
target_link_libraries(umpire-ui
    PUBLIC  umpire-app
    PRIVATE umpire-grid-ui dgl-opengl dgl-opengl-definitions umpire-imgui umpire-warnings)
if(APPLE)
    target_compile_definitions(umpire-ui PRIVATE GL_SILENCE_DEPRECATION)
else()
    # The windows' icon (_NET_WM_ICON), embedded: tools/render-linux-icons.sh made them.
    include(cmake/EmbedFile.cmake)
    embed_file("${CMAKE_SOURCE_DIR}/resources/linux/icons/umpire-daw-128.png"
               "${CMAKE_BINARY_DIR}/WindowIcon128.cpp" kWindowIcon128)
    embed_file("${CMAKE_SOURCE_DIR}/resources/linux/icons/umpire-daw-48.png"
               "${CMAKE_BINARY_DIR}/WindowIcon48.cpp" kWindowIcon48)
    target_sources(umpire-ui PRIVATE src/Ui/WindowIcon.cpp
                   "${CMAKE_BINARY_DIR}/WindowIcon128.cpp" "${CMAKE_BINARY_DIR}/WindowIcon48.cpp")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
    target_link_libraries(umpire-ui PRIVATE PkgConfig::X11)
endif()

# --- umpire-daw: the app ---------------------------------------------------
add_executable(umpire-daw src/App/main.cpp)
target_link_libraries(umpire-daw PRIVATE umpire-ui umpire-app uw::midi umpire-warnings)

if(APPLE)
    # Umpire.app (BUILDING.md, "macOS"): just enough of a bundle for a Dock/Finder/
    # Cmd-Tab icon (StageHand's CMakeLists.txt is the template this follows). No
    # document types yet: those come with milestone 9 (Files).
    set_target_properties(umpire-daw PROPERTIES
        OUTPUT_NAME                        Umpire
        MACOSX_BUNDLE                      TRUE
        MACOSX_BUNDLE_INFO_PLIST           "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME          Umpire
        MACOSX_BUNDLE_GUI_IDENTIFIER       com.unlikelyware.umpire
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}")
    # The icon, compiled from resources/Umpire.icon (an Icon Composer document) by
    # Xcode's actool: Assets.car, which macOS 26 draws (its dark and tinted versions
    # too), and Umpire.icns, which actool still writes. Without Xcode (only the
    # Command Line Tools) the bundle has no icon. Nor with an Xcode before 26: Icon
    # Composer documents are new in 26, and an older actool reports success and
    # writes neither file.
    execute_process(COMMAND xcrun --find actool OUTPUT_VARIABLE UMPIRE_ACTOOL
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE actool_missing)
    set(actool_version 0)
    if(NOT actool_missing)
        execute_process(COMMAND "${UMPIRE_ACTOOL}" --version OUTPUT_VARIABLE actool_info ERROR_QUIET)
        if(actool_info MATCHES "<key>short-bundle-version</key>[ \t\r\n]*<string>([0-9.]+)</string>")
            set(actool_version "${CMAKE_MATCH_1}")
        endif()
    endif()
    if(actool_missing)
        message(WARNING "Xcode's actool not found: Umpire.app will have no icon "
                        "(install Xcode, not just the Command Line Tools)")
    elseif(actool_version VERSION_LESS 26)
        message(WARNING "Xcode ${actool_version}'s actool can't compile Icon Composer documents: "
                        "Umpire.app will have no icon (Xcode 26 or later makes it)")
    else()
        set(icon "${CMAKE_SOURCE_DIR}/resources/Umpire.icon")
        set(icon_out "${CMAKE_BINARY_DIR}/icon")
        file(GLOB_RECURSE icon_inputs CONFIGURE_DEPENDS "${icon}/*")
        add_custom_command(
            OUTPUT "${icon_out}/Assets.car" "${icon_out}/Umpire.icns"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${icon_out}"
            COMMAND "${UMPIRE_ACTOOL}" "${icon}" --compile "${icon_out}" --app-icon Umpire
                    --platform macosx --target-device mac
                    --minimum-deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
                    --output-partial-info-plist "${icon_out}/partial.plist"
                    --errors --warnings --output-format human-readable-text
            DEPENDS ${icon_inputs}
            COMMENT "Compiling the app icon"
            VERBATIM)
        set(icon_files "${icon_out}/Assets.car" "${icon_out}/Umpire.icns")
        target_sources(umpire-daw PRIVATE ${icon_files})
        set_source_files_properties(${icon_files} PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
    endif()
    # Ad-hoc signed once whole: the linker signs only the executable, not the
    # Resources added after it.
    add_custom_command(TARGET umpire-daw POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:umpire-daw>"
        VERBATIM)
    # build/umpire-daw stays the command line's: a script running the bundle's
    # binary by its real path (BUILDING.md and the milestone docs document this
    # path). Not a link: run through one, the binary isn't seen as the bundle's
    # (no bundle id, name or Info.plist, so no Dock icon).
    file(REMOVE "${CMAKE_BINARY_DIR}/umpire-daw")   # a plain executable, from before this bundle existed
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/umpire-daw" @ONLY CONTENT
"#!/bin/sh\nexec \"@CMAKE_BINARY_DIR@/Umpire.app/Contents/MacOS/Umpire\" \"$@\"\n")
    file(CHMOD "${CMAKE_BINARY_DIR}/umpire-daw"
         PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

# --- Umpire as a plugin (milestones P1 and P2, docs/plugin-version.md) ------------
# A CLAP instrument with its own entry point, as Retune's:
# it opens its own MIDI input and hosts a CLAP synth through uw-core, so the
# notes never pass through the host. DGL and ImGui only for its window (P3),
# as the app's grid uses them; no DPF plugin wrapper. clap-wrapper (Unlikelyware's fork, `unlikelyware`, as
# Retune pins it) makes the VST3, for Ardour, and the AUv2 on the Mac, for Logic.
option(UMPIRE_BUILD_PLUGIN "Build Umpire.clap" ON)
option(UMPIRE_BUILD_VST3 "Build Umpire.vst3 via clap-wrapper" ON)
option(UMPIRE_BUILD_AUV2 "Build Umpire.component (AUv2 instrument) via clap-wrapper (macOS)" ${APPLE})
if(UMPIRE_BUILD_AUV2 AND NOT APPLE)
    message(FATAL_ERROR "UMPIRE_BUILD_AUV2 is macOS-only. Reconfigure with -DUMPIRE_BUILD_AUV2=OFF.")
endif()

if(UMPIRE_BUILD_PLUGIN)
    # The engine, the lane and its thread, and the setup, without CLAP: the
    # tests drive them directly.
    add_library(umpire-plugin-core STATIC
        src/Plugin/PluginEngine.cpp
        src/Plugin/Lane.cpp
        src/Plugin/LaneKeeper.cpp
        src/Plugin/PluginSetup.cpp)
    target_link_libraries(umpire-plugin-core PUBLIC umpire-clip uw::plugin uw::note-effects Threads::Threads PRIVATE umpire-warnings)
    set_target_properties(umpire-plugin-core PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

    # The title row's logo and the Help window's page (help.md), compiled in as bytes, as the
    # other Unlikelyware plugins'. Configure-time (cmake/EmbedFile.cmake): editing either
    # re-runs configure.
    include(cmake/EmbedFile.cmake)
    embed_file("${CMAKE_SOURCE_DIR}/resources/Unlikelyware-Final-Color.png"
               "${CMAKE_BINARY_DIR}/UnlikelywareLogo.cpp" kUnlikelywareLogo)
    embed_file("${CMAKE_SOURCE_DIR}/resources/help.md" "${CMAKE_BINARY_DIR}/UmpireHelp.cpp" kUmpireHelp)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${CMAKE_SOURCE_DIR}/resources/Unlikelyware-Final-Color.png" "${CMAKE_SOURCE_DIR}/resources/help.md")

    # The plugin's own sources, compiled into each binary: the .clap and the .vst3.
    function(umpire_add_plugin_sources TARGET_NAME)
        # The window (P3, Plugin/PluginEditor.h): the grid's UI, DGL and ImGui.
        target_sources(${TARGET_NAME} PRIVATE src/Plugin/UmpirePlugin.cpp src/Plugin/PluginEditor.cpp
                       "${CMAKE_BINARY_DIR}/UnlikelywareLogo.cpp" "${CMAKE_BINARY_DIR}/UmpireHelp.cpp")
        # P4: Export take and Import clip use the app's file dialogs (docs/files.md).
        if(APPLE)
            target_sources(${TARGET_NAME} PRIVATE src/Plugin/PluginEditorMac.mm src/App/FileDialogMac.mm)
            target_link_libraries(${TARGET_NAME} PRIVATE "-framework UniformTypeIdentifiers")
            target_compile_definitions(${TARGET_NAME} PRIVATE GL_SILENCE_DEPRECATION)
        else()
            target_sources(${TARGET_NAME} PRIVATE src/App/FileDialog.cpp)
        endif()
        target_link_libraries(${TARGET_NAME} PRIVATE umpire-plugin-core umpire-grid-ui uw::clap-host uw::midi
                              uw::editor-window umpire-warnings)
        target_compile_definitions(${TARGET_NAME} PRIVATE UMPIRE_VERSION_STRING="${PROJECT_VERSION}")
        set_target_properties(${TARGET_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    endfunction()

    add_library(umpire-clap MODULE)
    umpire_add_plugin_sources(umpire-clap)
    # clap_entry is the only export: another plugin's uw-core in the same host can't bind to ours.
    if(APPLE)
        target_link_options(umpire-clap PRIVATE "LINKER:-exported_symbol,_clap_entry")
        set_target_properties(umpire-clap PROPERTIES
            OUTPUT_NAME                        Umpire
            BUNDLE                             TRUE
            BUNDLE_EXTENSION                   clap
            MACOSX_BUNDLE_INFO_PLIST           "${CMAKE_SOURCE_DIR}/cmake/clap-Info.plist.in"
            MACOSX_BUNDLE_GUI_IDENTIFIER       com.unlikelyware.umpire.clap
            MACOSX_BUNDLE_BUNDLE_NAME          Umpire
            MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
        set(UMPIRE_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
        set(UMPIRE_CLAP_ARTIFACT "$<TARGET_BUNDLE_DIR:umpire-clap>")
    else()
        file(WRITE "${CMAKE_BINARY_DIR}/clap-exports.map" "{ global: clap_entry; local: *; };\n")
        target_link_options(umpire-clap PRIVATE "LINKER:--version-script=${CMAKE_BINARY_DIR}/clap-exports.map")
        set_target_properties(umpire-clap PROPERTIES OUTPUT_NAME Umpire PREFIX "" SUFFIX ".clap")
        set(UMPIRE_CLAP_DIR "$ENV{HOME}/.clap")
        set(UMPIRE_CLAP_ARTIFACT "$<TARGET_FILE:umpire-clap>")
    endif()
    # A separate target, as Retune's, so a build never writes outside the build tree.
    add_custom_target(install-clap
        COMMAND ${CMAKE_COMMAND} -E make_directory "${UMPIRE_CLAP_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${UMPIRE_CLAP_DIR}/Umpire.clap"
        COMMAND ${CMAKE_COMMAND} -E create_symlink "${UMPIRE_CLAP_ARTIFACT}" "${UMPIRE_CLAP_DIR}/Umpire.clap"
        DEPENDS umpire-clap
        VERBATIM
        COMMENT "Symlinking Umpire.clap into ${UMPIRE_CLAP_DIR}")

    if(UMPIRE_BUILD_VST3 OR UMPIRE_BUILD_AUV2)
        if(APPLE)
            enable_language(OBJC)   # the wrappers' Objective-C sources
        endif()
        set(CLAP_SDK_ROOT "${CMAKE_SOURCE_DIR}/external/uw-core/external/clap" CACHE PATH "" FORCE)
        set(VST3_SDK_ROOT "${CMAKE_SOURCE_DIR}/external/vst3sdk" CACHE PATH "" FORCE)
        if(UNIX AND NOT APPLE)
            set(TCLP_RESOURCE_DIRECTORY "")   # clap-wrapper's Linux branch tests a misspelled name (as Retune)
        endif()
        if(UMPIRE_BUILD_AUV2 AND NOT AUDIOUNIT_SDK_ROOT)
            # clap-wrapper fetches Apple's AudioUnitSDK itself unless AUDIOUNIT_SDK_ROOT names a copy
            # (Retune's build/cpm/AudioUnitSDK will do).
            set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON)
        endif()
        add_subdirectory(external/clap-wrapper EXCLUDE_FROM_ALL)
    endif()

    if(UMPIRE_BUILD_VST3)
        add_library(umpire-vst3 MODULE)
        umpire_add_plugin_sources(umpire-vst3)
        target_add_vst3_wrapper(
            TARGET            umpire-vst3
            OUTPUT_NAME       "Umpire"
            BUNDLE_IDENTIFIER "com.unlikelyware.umpire.vst3"
            BUNDLE_VERSION    "${PROJECT_VERSION}")
        if(TARGET base-sdk-vst3)
            target_compile_options(base-sdk-vst3 PRIVATE -Wno-deprecated-declarations)   # std::wstring_convert
        endif()
        if(APPLE)
            set(UMPIRE_VST3_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
            set(UMPIRE_VST3_ARTIFACT "$<TARGET_BUNDLE_DIR:umpire-vst3>")
        else()
            set(UMPIRE_VST3_DIR "$ENV{HOME}/.vst3")
            # The bundle folder, which clap-wrapper puts in Debug/ or Release/ (as Retune).
            set(UMPIRE_VST3_ARTIFACT "${CMAKE_BINARY_DIR}/$<IF:$<CONFIG:Debug>,Debug,Release>/Umpire.vst3")
        endif()
        add_custom_target(install-vst3
            COMMAND ${CMAKE_COMMAND} -E make_directory "${UMPIRE_VST3_DIR}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${UMPIRE_VST3_DIR}/Umpire.vst3"
            COMMAND ${CMAKE_COMMAND} -E create_symlink "${UMPIRE_VST3_ARTIFACT}" "${UMPIRE_VST3_DIR}/Umpire.vst3"
            DEPENDS umpire-vst3
            VERBATIM
            COMMENT "Symlinking Umpire.vst3 into ${UMPIRE_VST3_DIR}")
    endif()

    if(UMPIRE_BUILD_AUV2)
        # For Logic, which loads only Audio Units. "aumu", an instrument (from the CLAP
        # features); "Umpr" under Unlikelyware's "nLKL", as Retune's "Rtun".
        add_library(umpire-auv2 MODULE)
        umpire_add_plugin_sources(umpire-auv2)
        target_add_auv2_wrapper(
            TARGET            umpire-auv2
            OUTPUT_NAME       "Umpire"
            BUNDLE_IDENTIFIER "com.unlikelyware.umpire.auv2"
            BUNDLE_VERSION    "${PROJECT_VERSION}"
            MANUFACTURER_NAME "Unlikelyware"
            MANUFACTURER_CODE "nLKL"
            SUBTYPE_CODE      "Umpr"
            INSTRUMENT_TYPE   "aumu")
        # Copied, not symlinked, as Retune's and PlainOldSynth's: Logic's AU scan wants the bundle
        # itself. Then `auval -v aumu Umpr nLKL` (BUILDING.md).
        set(UMPIRE_AUV2_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
        add_custom_target(install-auv2
            COMMAND ${CMAKE_COMMAND} -E make_directory "${UMPIRE_AUV2_DIR}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${UMPIRE_AUV2_DIR}/Umpire.component"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "$<TARGET_BUNDLE_DIR:umpire-auv2>" "${UMPIRE_AUV2_DIR}/Umpire.component"
            # The Info.plist with AudioComponents, which macOS needs to see an Audio Unit at
            # all. clap-wrapper copies it into the bundle only when the binary links; a CMake
            # re-run afterwards rewrites the bundle's from the plain template (2026-11-20:
            # Logic then said Umpire wasn't available). The signature doesn't cover it.
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${CMAKE_CURRENT_BINARY_DIR}/umpire-auv2-build-helper-output/auv2_Info.plist"
                    "${UMPIRE_AUV2_DIR}/Umpire.component/Contents/Info.plist"
            DEPENDS umpire-auv2
            VERBATIM
            COMMENT "Copying Umpire.component into ${UMPIRE_AUV2_DIR}")
    endif()
endif()

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
umpire_add_test(test-scale            tests/ScaleTests.cpp)
umpire_add_test(test-playback         tests/PlaybackTests.cpp)
umpire_add_test(test-audio-engine     tests/AudioEngineTests.cpp)
umpire_add_test(test-files           tests/FileTests.cpp)
umpire_add_test(test-clip-edit       tests/ClipEditTests.cpp)
umpire_add_test(test-zoom            tests/ZoomTests.cpp)

if(UMPIRE_BUILD_PLUGIN)
    add_executable(test-plugin-engine tests/PluginEngineTests.cpp)
    target_link_libraries(test-plugin-engine PRIVATE umpire-plugin-core umpire-warnings)
    add_test(NAME test-plugin-engine COMMAND test-plugin-engine)

    # The built .clap, loaded through uw-core's CLAP host as a host would: no
    # synth and no MIDI input (its setup file says none), so silent.
    add_executable(test-plugin-clap tests/PluginClapTests.cpp)
    target_link_libraries(test-plugin-clap PRIVATE umpire-plugin-core uw::clap-host umpire-warnings)
    add_dependencies(test-plugin-clap umpire-clap)
    add_test(NAME test-plugin-clap COMMAND test-plugin-clap "${UMPIRE_CLAP_ARTIFACT}")
    file(WRITE "${CMAKE_BINARY_DIR}/test-plugin.conf" "input = none\nsynth = none\n")
    set_tests_properties(test-plugin-clap PROPERTIES ENVIRONMENT
        "UMPIRE_PLUGIN_CONF=${CMAKE_BINARY_DIR}/test-plugin.conf;UMPIRE_PLUGIN_LOG=none")
endif()
if(UMPIRE_BUILD_PLUGIN)
    # The built .clap hosting PlainOldSynth, rendering into buffers only, so
    # silent; skipped (77) where PlainOldSynth isn't installed.
    add_executable(test-plugin-render tests/PluginRenderTests.cpp)
    target_link_libraries(test-plugin-render PRIVATE umpire-plugin-core uw::clap-host umpire-warnings)
    add_dependencies(test-plugin-render umpire-clap)
    add_test(NAME test-plugin-render COMMAND test-plugin-render "${UMPIRE_CLAP_ARTIFACT}")
    file(WRITE "${CMAKE_BINARY_DIR}/test-plugin-render.conf" "input = none\nsynth = PlainOldSynth\n")
    set_tests_properties(test-plugin-render PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT
        "UMPIRE_PLUGIN_CONF=${CMAKE_BINARY_DIR}/test-plugin-render.conf;UMPIRE_PLUGIN_LOG=none")
endif()

if(UMPIRE_BUILD_PLUGIN AND APPLE)
    # The plugin's window, checked by eye (milestone P3): a bare Cocoa host
    # opens Umpire.clap's window and clicks into it (tools/PluginWindowCheck.mm).
    # Not a test, since it opens a window; built only when asked:
    #   ninja -C build umpire-plugin-window-check
    #   UMPIRE_PLUGIN_CONF=build/test-plugin.conf UMPIRE_PLUGIN_LOG=none \
    #       build/umpire-plugin-window-check build/Umpire.clap DIR
    add_executable(umpire-plugin-window-check EXCLUDE_FROM_ALL tools/PluginWindowCheck.mm)
    target_link_libraries(umpire-plugin-window-check PRIVATE umpire-plugin-core "-framework Cocoa" umpire-warnings)
    target_include_directories(umpire-plugin-window-check PRIVATE external/uw-core/external/clap/include)
    set_source_files_properties(tools/PluginWindowCheck.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    add_dependencies(umpire-plugin-window-check umpire-clap)
endif()
