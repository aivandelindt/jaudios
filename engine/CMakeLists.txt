# Engine static libraries (juce-standalone-scaffold step 2).
# A library with no sources yet is declared INTERFACE so dependants can already link it; it
# becomes STATIC as soon as its module lands (one module per PR).

function(jaudios_engine_lib name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    if(ARG_SOURCES)
        add_library(${name} STATIC ${ARG_SOURCES})
        target_include_directories(${name} PUBLIC ${PROJECT_SOURCE_DIR})
        target_link_libraries(${name} PUBLIC ${ARG_DEPS} PRIVATE jaudios_warnings)
    else()
        add_library(${name} INTERFACE)
        target_include_directories(${name} INTERFACE ${PROJECT_SOURCE_DIR})
        target_link_libraries(${name} INTERFACE ${ARG_DEPS})
    endif()
endfunction()

include(theory/sources.cmake)
include(analysis/sources.cmake)

jaudios_engine_lib(engine_theory     SOURCES ${ENGINE_THEORY_SOURCES})
jaudios_engine_lib(engine_analysis   SOURCES ${ENGINE_ANALYSIS_SOURCES} DEPS engine_theory jaudios_juce)
jaudios_engine_lib(engine_generators DEPS engine_theory)
jaudios_engine_lib(engine_synth      DEPS jaudios_juce)
jaudios_engine_lib(engine_fx         DEPS jaudios_juce)
jaudios_engine_lib(engine_export     DEPS jaudios_juce)
