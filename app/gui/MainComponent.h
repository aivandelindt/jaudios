#pragma once

#include "SongView.h"
#include "Theme.h"

#include "app/core/AnalysisService.h"
#include "app/core/AudioEngine.h"
#include "app/core/ExportService.h"
#include "app/core/SongController.h"
#include "app/core/SongPipeline.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios
{

/** The app window's content: transport bar (open, play, position, key / tempo, style,
    progression, undo / redo, audio settings), the song editor and a status line. A dropped or
    opened file is imported and analysed on the analysis worker (analyseSong: key, beats,
    transcription, suggestions); the song then plays through the engine. */
class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void importFile (const juce::File& file);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct ImportJobResult
    {
        analysis::ImportResult import;
        gui::WaveformPeaks peaks;
        std::optional<SongAnalysis> song;
        juce::String error;
    };

    void timerCallback() override;
    void showAudioSettings();
    void showOpenDialog();
    void onImported (ImportJobResult result);
    void refreshTransport();
    void setStatus (const juce::String&);

    gui::LookAndFeel lookAndFeel;
    AudioEngine engine;
    AnalysisService analysisService;
    analysis::AudioImporter importer; // format list is read-only after construction: safe on both threads
    analysis::KeyDetectorHandle keyDetector = analysis::makeKeyDetector (analysis::AppConfig::fromEnvironment());
    DragSession dragSession;
    std::unique_ptr<SongController> song;

    juce::TextButton openButton { "Open..." }, playButton { "Play" }, undoButton { "Undo" }, redoButton { "Redo" },
        settingsButton { "Audio..." };
    juce::Label positionLabel, songLabel, statusLabel;
    juce::ComboBox styleBox, progressionBox;
    gui::SongView view;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::String deviceError, songName;
    bool dragHover = false, importing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace jaudios
