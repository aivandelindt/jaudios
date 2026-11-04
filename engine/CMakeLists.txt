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
# Dev location of the OpenKeyScan sidecar unpacked by `make models` (see models/manifest.json).
target_compile_definitions(engine_analysis PRIVATE
    JAUDIOS_DEV_SIDECAR_DIR="${PROJECT_BINARY_DIR}/sidecar/openkeyscan-analyzer"
    JAUDIOS_DEV_MODELS_DIR="${PROJECT_BINARY_DIR}/models")

# Phase B: ONNX Runtime + the model's expected sha256 from models/manifest.json.
file(READ "${PROJECT_SOURCE_DIR}/models/manifest.json" _manifest)
string(JSON _onnx_sha GET "${_manifest}" openkeyscan3_onnx sha256)
target_compile_definitions(engine_analysis PRIVATE JAUDIOS_OPENKEYSCAN_ONNX_SHA256="${_onnx_sha}")
if(JAUDIOS_WITH_ONNXRUNTIME)
    target_link_libraries(engine_analysis PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(engine_analysis PRIVATE JAUDIOS_HAS_ONNXRUNTIME=1)
endif()

jaudios_engine_lib(engine_generators DEPS engine_theory)
jaudios_engine_lib(engine_synth      DEPS jaudios_juce)
jaudios_engine_lib(engine_fx         DEPS jaudios_juce)
jaudios_engine_lib(engine_export     DEPS jaudios_juce)
