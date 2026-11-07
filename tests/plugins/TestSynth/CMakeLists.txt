# A tiny VST3 instrument for the hosting tests (never shipped). It compiles its own copy of the JUCE
# modules it needs (a plugin is a separate binary; jaudios_juce is built for the standalone app).
juce_add_plugin(JAudiosTestSynth
    PRODUCT_NAME "JAudios Test Synth"
    COMPANY_NAME "JAudios"
    BUNDLE_ID "app.jaudios.testsynth"
    PLUGIN_MANUFACTURER_CODE Jaud
    PLUGIN_CODE Jtst
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(JAudiosTestSynth PRIVATE TestSynth.cpp)
target_compile_definitions(JAudiosTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                                                   JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(JAudiosTestSynth PRIVATE juce::juce_audio_utils
                      PUBLIC juce::juce_recommended_config_flags)
set(JAUDIOS_TEST_VST3 "${CMAKE_CURRENT_BINARY_DIR}/JAudiosTestSynth_artefacts/${CMAKE_BUILD_TYPE}/VST3/JAudios Test Synth.vst3" PARENT_SCOPE)
