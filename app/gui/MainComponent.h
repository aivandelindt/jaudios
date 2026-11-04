#pragma once

#include "WaveformView.h"

#include "app/core/AnalysisService.h"
#include "app/core/AudioEngine.h"
#include "engine/analysis/AudioImport.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios
{

/** Scaffold UI: device settings, file import (drag-and-drop + Open), waveform, transport, status.
    The real layout comes with the ui-shell skill. */
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct ImportJobResult
    {
        analysis::ImportResult import;
        WaveformPeaks peaks;
    };

    void timerCallback() override;
    void showAudioSettings();
    void showOpenDialog();
    void importFile (const juce::File& file);
    void onImported (ImportJobResult result);
    void updateStatus();

    AudioEngine engine;
    AnalysisService analysisService;
    analysis::AudioImporter importer; // format list is read-only after construction: safe on both threads

    juce::TextButton settingsButton { "Audio settings..." };
    juce::TextButton openButton { "Open..." };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    WaveformView waveform;
    juce::Label statusLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String deviceError, importStatus;
    bool dragHover = false;
    bool importing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace jaudios
