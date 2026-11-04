#pragma once

#include "app/core/AnalysisService.h"
#include "app/core/AudioEngine.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios
{

/** Scaffold UI: device settings + transport + status. Real layout comes with the ui-shell skill. */
class MainComponent final : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showAudioSettings();
    void updateStatus();

    AudioEngine engine;
    AnalysisService analysis;

    juce::TextButton settingsButton { "Audio settings..." };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::Label statusLabel;

    juce::String deviceError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace jaudios
