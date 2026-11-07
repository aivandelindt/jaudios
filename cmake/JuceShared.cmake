# JUCE modules are CMake INTERFACE libraries that compile their sources into every consumer.
# Linking them into several static libs + executables would compile JUCE several times and
# produce duplicate symbols, so we compile them exactly once into `jaudios_juce` and re-export
# the include dirs / definitions to everything that links it.

add_library(jaudios_juce STATIC)

target_link_libraries(jaudios_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_compile_definitions(jaudios_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        # MP3/M4A: CoreAudioFormat on macOS. JUCE's software MP3 decoder stays off (see the
        # JUCE_USE_MP3AUDIOFORMAT legal disclaimer in juce_audio_formats.h).
        JUCE_USE_MP3AUDIOFORMAT=0
        # synth-and-vst3-host: VST3 instruments on the generated channels (JUCE's bundled VST3 SDK
        # 3.8.0, MIT). AU / LV2 / LADSPA stay off.
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_JACK=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1
        JUCE_MODAL_LOOPS_PERMITTED=0)

target_include_directories(jaudios_juce SYSTEM
    INTERFACE $<TARGET_PROPERTY:jaudios_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(jaudios_juce
    INTERFACE $<TARGET_PROPERTY:jaudios_juce,COMPILE_DEFINITIONS>)

set_target_properties(jaudios_juce PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

# Common warning set for our own code (not JUCE's).
add_library(jaudios_warnings INTERFACE)
if(MSVC)
    target_compile_options(jaudios_warnings INTERFACE /W4)
else()
    target_compile_options(jaudios_warnings INTERFACE -Wall -Wextra -Wshadow -Wconversion
                                                      -Wno-sign-conversion -Wno-float-conversion)
endif()
