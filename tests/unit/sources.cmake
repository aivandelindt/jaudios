set(JAUDIOS_TEST_SOURCES
    # juce-standalone-scaffold
    ${CMAKE_CURRENT_LIST_DIR}/test_spsc_queue.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_audio_engine.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_analysis_service.cpp
    # audio-capture (file import)
    ${CMAKE_CURRENT_LIST_DIR}/test_resampler.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_audio_import.cpp
    # key-and-tempo-analysis
    ${CMAKE_CURRENT_LIST_DIR}/test_key_theory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_key_detection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_tempo.cpp)
