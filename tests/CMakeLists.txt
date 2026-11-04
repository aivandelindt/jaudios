include(Catch)

# Debug/ASan runs: suppress only the known third-party leak in tests/lsan.supp.
set(JAUDIOS_TEST_ENV "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=0")

# Shared helpers (synthetic signals, temp files) for every test binary.
add_library(test_support INTERFACE)
target_include_directories(test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
set(JAUDIOS_CQT_REFERENCE_DIR "${PROJECT_BINARY_DIR}/goldens/cqt")
target_compile_definitions(test_support INTERFACE
    JAUDIOS_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    JAUDIOS_CQT_REFERENCE_DIR="${JAUDIOS_CQT_REFERENCE_DIR}"
    JAUDIOS_DEV_MODELS_DIR="${PROJECT_BINARY_DIR}/models")

# Python reference for the CQT / ONNX golden tests (ml/tests/cqt_reference.py). Optional: point it
# at a Python 3.12 env holding the pins in models/manifest.json; without it those tests SKIP.
set(JAUDIOS_ML_PYTHON "" CACHE FILEPATH "Python with librosa/torch pins for ML reference tests")

include(unit/sources.cmake)

add_executable(jaudios_tests TestMain.cpp ${JAUDIOS_TEST_SOURCES})
target_link_libraries(jaudios_tests PRIVATE Catch2::Catch2 app_core test_support jaudios_warnings)
catch_discover_tests(jaudios_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT "${JAUDIOS_TEST_ENV}")
if(JAUDIOS_WITH_ONNXRUNTIME)
    jaudios_copy_onnxruntime(jaudios_tests)
endif()

# Same tests, linked with the realtime trap (tools/rt_check.sh). Tests tagged [rt] assert zero
# allocations on the audio path; elsewhere they SKIP.
if(JAUDIOS_RT_CHECK)
    add_executable(jaudios_rt_tests TestMain.cpp ${JAUDIOS_TEST_SOURCES} $<TARGET_OBJECTS:app_rt_trap_hooks>)
    target_link_libraries(jaudios_rt_tests PRIVATE Catch2::Catch2 app_core test_support ${CMAKE_DL_LIBS})
    if(NOT APPLE AND NOT MSVC)
        target_link_options(jaudios_rt_tests PRIVATE -rdynamic) # readable JAUDIOS_RT_BACKTRACE output
    endif()
endif()

# Golden tests against ml/tests/goldens.json. Missing fixtures are rendered first (never overwritten).
add_executable(jaudios_golden_tests TestMain.cpp golden/test_goldens.cpp)
target_link_libraries(jaudios_golden_tests PRIVATE Catch2::Catch2 app_core test_support jaudios_warnings)
add_test(NAME render_missing_fixtures COMMAND fixture-render ${PROJECT_SOURCE_DIR}/tests/fixtures)
set_tests_properties(render_missing_fixtures PROPERTIES FIXTURES_SETUP golden_audio)
catch_discover_tests(jaudios_golden_tests DISCOVERY_MODE PRE_TEST
                     PROPERTIES FIXTURES_REQUIRED golden_audio ENVIRONMENT "${JAUDIOS_TEST_ENV}")

# C++ CQT / ONNX vs the Python reference (needs JAUDIOS_ML_PYTHON, else SKIPs).
add_executable(jaudios_ml_golden_tests TestMain.cpp golden/test_cqt_golden.cpp)
target_link_libraries(jaudios_ml_golden_tests PRIVATE Catch2::Catch2 app_core test_support jaudios_warnings)
catch_discover_tests(jaudios_ml_golden_tests DISCOVERY_MODE PRE_TEST
                     PROPERTIES FIXTURES_REQUIRED cqt_reference ENVIRONMENT "${JAUDIOS_TEST_ENV}")

if(JAUDIOS_ML_PYTHON)
    set(_fixture_wavs)
    foreach(name house_124_Amin techno_132_Fmin pop_100_Cmaj loop_8bar_Gmaj stem_bass_Dmin)
        list(APPEND _fixture_wavs ${PROJECT_SOURCE_DIR}/tests/fixtures/${name}.wav)
    endforeach()
    add_test(NAME cqt_reference
             COMMAND ${JAUDIOS_ML_PYTHON} ${PROJECT_SOURCE_DIR}/ml/tests/cqt_reference.py ${JAUDIOS_CQT_REFERENCE_DIR} ${_fixture_wavs})
    set_tests_properties(cqt_reference PROPERTIES FIXTURES_SETUP cqt_reference FIXTURES_REQUIRED golden_audio)
endif()
