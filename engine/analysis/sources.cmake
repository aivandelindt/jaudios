set(ENGINE_ANALYSIS_SOURCES
    # audio-capture (file import)
    ${CMAKE_CURRENT_LIST_DIR}/AudioImport.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Resampler.cpp
    # key-and-tempo-analysis
    ${CMAKE_CURRENT_LIST_DIR}/Stft.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Chroma.cpp
    ${CMAKE_CURRENT_LIST_DIR}/key/KeyDetector.cpp
    ${CMAKE_CURRENT_LIST_DIR}/key/KeyDetectorChroma.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tempo/BeatTracker.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Analysis.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Log.cpp
    # key-detection-openkeyscan Phase A (sidecar)
    ${CMAKE_CURRENT_LIST_DIR}/openkeyscan/OpenKeyScanProtocol.cpp
    ${CMAKE_CURRENT_LIST_DIR}/openkeyscan/SidecarProcess.cpp
    ${CMAKE_CURRENT_LIST_DIR}/openkeyscan/KeyDetectorSidecar.cpp
    # key-detection-openkeyscan Phase B (ONNX)
    ${CMAKE_CURRENT_LIST_DIR}/../dsp/CQT.cpp
    ${CMAKE_CURRENT_LIST_DIR}/openkeyscan/KeyDetectorOnnx.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../util/Sha256.cpp
    # stem-separation (HT-Demucs via ONNX Runtime)
    ${CMAKE_CURRENT_LIST_DIR}/stems/DemucsSpec.cpp
    ${CMAKE_CURRENT_LIST_DIR}/stems/StemSeparator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/stems/StemCache.cpp
    # audio-to-midi (Basic Pitch via ONNX Runtime)
    ${CMAKE_CURRENT_LIST_DIR}/transcription/NoteCreation.cpp
    ${CMAKE_CURRENT_LIST_DIR}/transcription/AudioToMidi.cpp)
