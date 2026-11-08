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
string(JSON _source_commit GET "${_manifest}" openkeyscan_source commit)
string(JSON _ckpt_sha GET "${_manifest}" openkeyscan3_pt sha256)
target_compile_definitions(engine_analysis PRIVATE
    JAUDIOS_OPENKEYSCAN_ONNX_SHA256="${_onnx_sha}"
    JAUDIOS_OPENKEYSCAN_SOURCE_COMMIT="${_source_commit}"
    JAUDIOS_OPENKEYSCAN_CHECKPOINT_SHA256="${_ckpt_sha}")
# stem-separation: htdemucs.onnx sha256 + the pins a local export's provenance must match.
string(JSON _demucs_sha GET "${_manifest}" htdemucs_onnx sha256)
string(JSON _demucs_ckpt GET "${_manifest}" htdemucs_pt sha256)
string(JSON _demucs_version GET "${_manifest}" htdemucs_pt demucs_version)
target_compile_definitions(engine_analysis PRIVATE
    JAUDIOS_HTDEMUCS_ONNX_SHA256="${_demucs_sha}"
    JAUDIOS_HTDEMUCS_CHECKPOINT_SHA256="${_demucs_ckpt}"
    JAUDIOS_DEMUCS_VERSION="${_demucs_version}")
# audio-to-midi: basic_pitch.onnx is upstream's file, so only its sha256 is accepted.
string(JSON _basic_pitch_sha GET "${_manifest}" basic_pitch_onnx sha256)
target_compile_definitions(engine_analysis PRIVATE JAUDIOS_BASIC_PITCH_ONNX_SHA256="${_basic_pitch_sha}")
if(JAUDIOS_WITH_ONNXRUNTIME)
    target_link_libraries(engine_analysis PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(engine_analysis PRIVATE JAUDIOS_HAS_ONNXRUNTIME=1)
endif()

# Generator pattern libraries (bass: generators/patterns, drums: generators/drums), embedded as
# string literals; validated by tools/validate_patterns.py (ctest + CI) and by the C++ parsers.
function(jaudios_embed_patterns dir symbol out)
    file(GLOB _files CONFIGURE_DEPENDS ${dir}/*.json)
    list(FILTER _files EXCLUDE REGEX "schema\\.json$")
    list(SORT _files)
    set(_src "// Generated from ${dir}/*.json by engine/CMakeLists.txt. Do not edit.\nnamespace jaudios::generators {\nextern const char* const ${symbol}[] = {\n")
    set(_count 0)
    foreach(_f ${_files})
        file(READ ${_f} _json)
        string(APPEND _src "R\"JAUDIOSPATTERN(${_json})JAUDIOSPATTERN\",\n")
        math(EXPR _count "${_count} + 1")
    endforeach()
    string(REGEX REPLACE "^embedded" "numEmbedded" _numSymbol "${symbol}")
    string(APPEND _src "};\nextern const int ${_numSymbol} = ${_count};\n}\n")
    file(CONFIGURE OUTPUT ${out} CONTENT "${_src}" @ONLY)
endfunction()
jaudios_embed_patterns(${CMAKE_CURRENT_LIST_DIR}/generators/patterns embeddedBassPatterns ${CMAKE_CURRENT_BINARY_DIR}/BassPatternsEmbedded.cpp)
jaudios_embed_patterns(${CMAKE_CURRENT_LIST_DIR}/generators/drums embeddedDrumPatterns ${CMAKE_CURRENT_BINARY_DIR}/DrumPatternsEmbedded.cpp)

# synth-and-vst3-host: built-in SFZ presets (engine/synth/presets/*.sfz), id = file name.
file(GLOB _sfz_presets CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/synth/presets/*.sfz)
list(SORT _sfz_presets)
set(_ids "")
set(_texts "")
set(_n 0)
foreach(_f ${_sfz_presets})
    get_filename_component(_id ${_f} NAME_WE)
    file(READ ${_f} _sfz)
    string(APPEND _ids "\"${_id}\",\n")
    string(APPEND _texts "R\"JAUDIOSSFZ(${_sfz})JAUDIOSSFZ\",\n")
    math(EXPR _n "${_n} + 1")
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/SfzPresetsEmbedded.cpp CONTENT
"// Generated from engine/synth/presets/*.sfz by engine/CMakeLists.txt. Do not edit.
namespace jaudios::synth {
extern const char* const embeddedSfzPresetIds[] = {
${_ids}};
extern const char* const embeddedSfzPresets[] = {
${_texts}};
extern const int numEmbeddedSfzPresets = ${_n};
}
" @ONLY)

jaudios_engine_lib(engine_generators
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/generators/ChordEngine.cpp
            ${CMAKE_CURRENT_LIST_DIR}/generators/BassGenerator.cpp
            ${CMAKE_CURRENT_LIST_DIR}/generators/DrumGenerator.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/BassPatternsEmbedded.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/DrumPatternsEmbedded.cpp
    DEPS engine_theory jaudios_juce)
jaudios_engine_lib(engine_synth
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/synth/DrumSampler.cpp
            ${CMAKE_CURRENT_LIST_DIR}/synth/SfzInstrument.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/SfzPresetsEmbedded.cpp
    DEPS engine_generators jaudios_juce)
target_link_libraries(engine_synth PRIVATE sfizz::sfizz)
jaudios_engine_lib(engine_fx         SOURCES fx/FxChain.cpp DEPS jaudios_juce)
jaudios_engine_lib(engine_export     SOURCES export/Export.cpp DEPS engine_theory jaudios_juce)
