# app_core: engine glue shared by the GUI app, the CLI harness and the tests.
add_library(app_core STATIC
    core/AnalysisService.cpp
    core/AudioEngine.cpp
    core/EngineProcessors.cpp
    core/ExportService.cpp
    core/HarmonySource.cpp
    core/InstrumentSlot.cpp
    core/Project.cpp
    core/PluginHost.cpp
    core/OfflineRender.cpp
    core/RealtimeTrap.cpp
    core/SongController.cpp
    core/SongPipeline.cpp)
target_include_directories(app_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(app_core
    PUBLIC  jaudios_juce engine_analysis engine_theory engine_generators engine_synth engine_fx engine_export
    PRIVATE jaudios_warnings)
target_compile_definitions(app_core PUBLIC JAUDIOS_VERSION_STRING="${PROJECT_VERSION}")

# Interposing hooks for tools/rt_check.sh. Object library so the symbols end up in the
# executable itself (needed for malloc interposition); never linked into the app.
add_library(app_rt_trap_hooks OBJECT core/RealtimeTrapHooks.cpp)
target_include_directories(app_rt_trap_hooks PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(app_rt_trap_hooks PRIVATE ${CMAKE_DL_LIBS})

# The UI (ui-shell) as a library: the app and the headless UI tests link the same components.
add_library(app_gui STATIC
    gui/ExternalDrag.cpp
    gui/Lanes.cpp
    gui/MainComponent.cpp
    gui/Panels.cpp
    gui/SongView.cpp
    gui/Theme.cpp
    gui/Timeline.cpp)
target_link_libraries(app_gui PUBLIC app_core PRIVATE jaudios_warnings)

if(JAUDIOS_BUILD_APP)
    juce_add_gui_app(JAudios
        PRODUCT_NAME "JAudios"
        COMPANY_NAME "JAudios"
        BUNDLE_ID "app.jaudios.standalone"
        VERSION ${PROJECT_VERSION}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "JAudios records audio you choose to capture for analysis.")

    target_sources(JAudios PRIVATE gui/Main.cpp)
    target_link_libraries(JAudios PRIVATE app_gui jaudios_warnings)
    if(JAUDIOS_WITH_ONNXRUNTIME)
        jaudios_copy_onnxruntime(JAudios)
    endif()
endif()

# Phase A: bundle the OpenKeyScan sidecar unpacked by `make models` (build/sidecar/openkeyscan-analyzer).
# macOS: Contents/Resources/sidecar/ (ditto keeps symlinks/signatures); elsewhere: resources/sidecar/.
option(JAUDIOS_BUNDLE_SIDECAR "Copy the key detection sidecar into the app bundle when present" ON)
set(JAUDIOS_SIDECAR_DIR "${PROJECT_BINARY_DIR}/sidecar/openkeyscan-analyzer")
if(JAUDIOS_BUILD_APP AND JAUDIOS_BUNDLE_SIDECAR)
    if(EXISTS "${JAUDIOS_SIDECAR_DIR}")
        if(APPLE)
            add_custom_command(TARGET JAudios POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:JAudios>/Resources/sidecar"
                COMMAND ditto "${JAUDIOS_SIDECAR_DIR}" "$<TARGET_BUNDLE_CONTENT_DIR:JAudios>/Resources/sidecar/openkeyscan-analyzer"
                COMMENT "Bundling key detection sidecar")
        else()
            add_custom_command(TARGET JAudios POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${JAUDIOS_SIDECAR_DIR}" "$<TARGET_FILE_DIR:JAudios>/resources/sidecar/openkeyscan-analyzer"
                COMMENT "Bundling key detection sidecar")
        endif()
    else()
        message(STATUS "Key detection sidecar not found at ${JAUDIOS_SIDECAR_DIR}: run `make models` and re-configure to bundle it")
    endif()
endif()
