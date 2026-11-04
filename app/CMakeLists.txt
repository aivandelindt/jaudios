# app_core: engine glue shared by the GUI app, the CLI harness and the tests.
add_library(app_core STATIC
    core/AnalysisService.cpp
    core/AudioEngine.cpp
    core/EngineProcessors.cpp
    core/OfflineRender.cpp
    core/RealtimeTrap.cpp)
target_include_directories(app_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(app_core
    PUBLIC  jaudios_juce engine_analysis engine_theory
    PRIVATE jaudios_warnings)
target_compile_definitions(app_core PUBLIC JAUDIOS_VERSION_STRING="${PROJECT_VERSION}")

# Interposing hooks for tools/rt_check.sh. Object library so the symbols end up in the
# executable itself (needed for malloc interposition); never linked into the app.
add_library(app_rt_trap_hooks OBJECT core/RealtimeTrapHooks.cpp)
target_include_directories(app_rt_trap_hooks PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(app_rt_trap_hooks PRIVATE ${CMAKE_DL_LIBS})

if(JAUDIOS_BUILD_APP)
    juce_add_gui_app(JAudios
        PRODUCT_NAME "JAudios"
        COMPANY_NAME "JAudios"
        BUNDLE_ID "app.jaudios.standalone"
        VERSION ${PROJECT_VERSION}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "JAudios records audio you choose to capture for analysis.")

    target_sources(JAudios PRIVATE
        gui/Main.cpp
        gui/MainComponent.cpp)

    target_link_libraries(JAudios PRIVATE app_core jaudios_warnings)
endif()
