#include "MainComponent.h"

namespace jaudios
{

namespace gen = generators;

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    for (auto* b : { &openButton, &recordButton, &playButton, &undoButton, &redoButton, &settingsButton })
        addAndMakeVisible (b);
    addAndMakeVisible (meter);
    recordButton.setTooltip ("Record what your DAW or any app plays (R)");
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    recordButton.onClick = [this] {
        if (recorder.isRecording())
            stopCapture();
        else
            showCaptureMenu(); // the button always asks; R reuses the last source
    };
    for (auto* l : { &positionLabel, &songLabel, &statusLabel })
        addAndMakeVisible (l);
    addAndMakeVisible (styleBox);
    addAndMakeVisible (progressionBox);
    addAndMakeVisible (view);

    positionLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    positionLabel.setJustificationType (juce::Justification::centred);
    songLabel.setColour (juce::Label::textColourId, gui::colours::textDim);
    statusLabel.setColour (juce::Label::textColourId, gui::colours::textDim);
    for (int s = 0; s < 7; ++s)
        styleBox.addItem (juce::String (gen::styleName ((gen::Style) s)).toUpperCase().substring (0, 1)
                              + juce::String (gen::styleName ((gen::Style) s)).substring (1), s + 1);
    styleBox.setSelectedId (1, juce::dontSendNotification);
    styleBox.setTooltip ("Style: picks the progressions, the bass and drum patterns and the sounds");
    progressionBox.setTooltip ("Suggested progressions, best first");

    openButton.onClick = [this] { showOpenDialog(); };
    playButton.onClick = [this] { view.togglePlay(); };
    settingsButton.onClick = [this] { showAudioSettings(); };
    undoButton.onClick = [this] { if (song != nullptr) song->undoManager().undo(); };
    redoButton.onClick = [this] { if (song != nullptr) song->undoManager().redo(); };
    styleBox.onChange = [this] {
        if (song != nullptr)
            song->setStyle ((gen::Style) (styleBox.getSelectedId() - 1));
    };
    progressionBox.onChange = [this] {
        if (song != nullptr && progressionBox.getSelectedId() > 0 && progressionBox.getSelectedId() - 1 != song->progressionIndex())
            song->selectProgression (progressionBox.getSelectedId() - 1);
    };
    view.onStatus = [this] (const juce::String& s) { setStatus (s); };
    view.onRecordKey = [this] { toggleRecording(); };
    view.onSongChanged = [this] { refreshTransport(); };

    deviceError = engine.openDefaultDevice (AudioEngine::defaultBlockSize);
    setStatus (deviceError.isNotEmpty() ? "Audio device: " + deviceError : juce::String ("Ready"));
    refreshTransport();
    setWantsKeyboardFocus (true); // R and Space before a song is loaded
    setSize (1280, 640);
    startTimerHz (15);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (captureSource != nullptr)
        captureSource->stop();
    analysisService.cancelAll();
    view.setSong (nullptr, {}, {});
    song.reset();
    engine.closeDevice();
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (gui::colours::background);
    g.setColour (gui::colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (48));
    if (dragHover)
    {
        g.setColour (gui::colours::source);
        g.drawRect (view.getBounds().expanded (2), 2);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop (48).reduced (8, 8);
    for (auto* b : { &openButton, &recordButton, &playButton })
    {
        b->setBounds (bar.removeFromLeft (76));
        bar.removeFromLeft (6);
    }
    meter.setBounds (bar.removeFromLeft (8).reduced (0, 2));
    bar.removeFromLeft (4);
    positionLabel.setBounds (bar.removeFromLeft (120));
    bar.removeFromLeft (6);
    settingsButton.setBounds (bar.removeFromRight (76));
    bar.removeFromRight (12);
    redoButton.setBounds (bar.removeFromRight (60));
    bar.removeFromRight (4);
    undoButton.setBounds (bar.removeFromRight (60));
    bar.removeFromRight (12);
    progressionBox.setBounds (bar.removeFromRight (260));
    bar.removeFromRight (6);
    styleBox.setBounds (bar.removeFromRight (110));
    bar.removeFromRight (12);
    songLabel.setBounds (bar);
    statusLabel.setBounds (area.removeFromBottom (24).reduced (8, 2));
    view.setBounds (area);
}

void MainComponent::setStatus (const juce::String& s) { statusLabel.setText (s, juce::dontSendNotification); }

void MainComponent::refreshTransport()
{
    const bool has = song != nullptr;
    for (juce::Component* c : { (juce::Component*) &playButton, (juce::Component*) &styleBox, (juce::Component*) &progressionBox })
        c->setEnabled (has);
    undoButton.setEnabled (has && song->undoManager().canUndo());
    redoButton.setEnabled (has && song->undoManager().canRedo());
    if (! has)
    {
        songLabel.setText ("No song", juce::dontSendNotification);
        return;
    }
    const auto& a = song->song();
    songLabel.setText (songName + "   " + juce::String (theory::traditionalName (a.source.key)) + "   "
                           + juce::String (a.analysis.rhythm.bpm, 1) + " BPM",
                       juce::dontSendNotification);
    styleBox.setSelectedId ((int) song->style() + 1, juce::dontSendNotification);
    progressionBox.clear (juce::dontSendNotification);
    for (int i = 0; i < song->numProgressions(); ++i)
        progressionBox.addItem (juce::String (i + 1) + ".  " + song->suggestion (i).summary (a.source.key), i + 1);
    progressionBox.setSelectedId (song->progressionIndex() + 1, juce::dontSendNotification);
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    const bool interested = files.size() == 1 && importer.isSupportedFile (juce::File (files[0]));
    if (interested != dragHover)
    {
        dragHover = interested;
        repaint();
    }
    return interested;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    if (! files.isEmpty())
        importFile (juce::File (files[0]));
}

void MainComponent::showOpenDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a song", juce::File(), importer.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult().existsAsFile())
                                  importFile (fc.getResult());
                          });
}

void MainComponent::importFile (const juce::File& file)
{
    analyse ([this, file] { return importer.importFile (file); }, file.getFileName());
}

void MainComponent::analyse (std::function<analysis::ImportResult()> load, const juce::String& what)
{
    if (importing)
        return;
    importing = true;
    setStatus ("Analysing " + what + " (key, beats, notes, chord suggestions)...");
    SongSettings settings;
    settings.engine.style = (gen::Style) juce::jmax (0, styleBox.getSelectedId() - 1);
    analysisService.submit<ImportJobResult> (
        [this, load, settings] {
            ImportJobResult r;
            r.import = load();
            if (r.import.ok())
            {
                r.peaks = gui::WaveformPeaks::compute (r.import.capture->buffer, r.import.capture->sampleRate);
                r.song = analyseSong (*r.import.capture, settings, r.error, keyDetector.detector.get());
            }
            return r;
        },
        [this] (ImportJobResult r) { onImported (std::move (r)); });
}

void MainComponent::onImported (ImportJobResult r)
{
    importing = false;
    if (! r.import.ok())
    {
        setStatus ("Could not open the file: " + r.import.error);
        return;
    }
    if (! r.song)
    {
        setStatus ("Analysis failed: " + r.error);
        return;
    }
    auto& capture = *r.import.capture;
    songName = capture.sourceFile != juce::File() ? capture.sourceFile.getFileNameWithoutExtension()
                                                  : "Capture " + juce::Time (capture.startTimeMs).formatted ("%H.%M.%S");
    engine.stop();
    view.setSong (nullptr, {}, {});
    song.reset();
    song = std::make_unique<SongController> (engine, std::make_shared<const SongAnalysis> (std::move (*r.song)),
                                             std::make_shared<const juce::AudioBuffer<float>> (std::move (capture.buffer)),
                                             (gen::Style) juce::jmax (0, styleBox.getSelectedId() - 1));
    song->setSongName (songName);
    view.setSong (song.get(), std::move (r.peaks), dragSession.directory());
    view.grabKeyboardFocus();
    refreshTransport();
    setStatus ("Click a chord for alternatives, drag a tile into your DAW. Space plays, Cmd+Z undoes.");
}

void MainComponent::timerCallback()
{
    if (recorder.isRecording())
    {
        const int s = (int) recorder.elapsedSeconds();
        positionLabel.setText (juce::String::formatted ("REC %d:%02d", s / 60, s % 60), juce::dontSendNotification);
        meter.level = juce::jmax (meter.level * 0.7f, recorder.takePeak());
        meter.repaint();
        recordButton.setButtonText ("Stop");
        return;
    }
    if (song == nullptr)
    {
        positionLabel.setText ("--", juce::dontSendNotification);
        return;
    }
    const auto& player = engine.getSourcePlayer();
    const double t = (double) player.getPosition() / song->song().sampleRate;
    const double beat = std::max (0.0, song->grid().toBeats (t) + 1e-6); // bar 1 may start a few ms in
    const int bar = (int) std::floor (beat / song->grid().beatsPerBar()) + 1;
    const int beatInBar = (int) std::floor (std::fmod (std::max (0.0, beat), (double) song->grid().beatsPerBar())) + 1;
    positionLabel.setText (juce::String (bar) + "." + juce::String (beatInBar), juce::dontSendNotification);
    playButton.setButtonText (player.isPlaying() ? "Stop" : "Play");
    undoButton.setEnabled (song->undoManager().canUndo());
    redoButton.setEnabled (song->undoManager().canRedo());
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == 'r' || key.getKeyCode() == 'R')
        return toggleRecording(), true;
    if (key == juce::KeyPress::spaceKey)
        return view.togglePlay(), true;
    return false;
}

void MainComponent::toggleRecording()
{
    if (recorder.isRecording())
        stopCapture();
    else if (lastCaptureSource.isNotEmpty())
        startCapture (lastCaptureSource);
    else
        showCaptureMenu();
}

void MainComponent::showCaptureMenu()
{
    juce::String error;
    const auto sources = listCaptureSources (error);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Record from");
    int id = 1;
    juce::String kind;
    for (const auto& s : sources)
    {
        if (s.kind != kind && id > 1)
            menu.addSeparator();
        kind = s.kind;
        menu.addItem (id++, s.name, true, s.id == lastCaptureSource);
    }
    if (sources.empty())
        menu.addItem (-1, "No capture source: install BlackHole, or use macOS 13+", false, false);
    if (error.isNotEmpty())
        menu.addItem (-2, error, false, false);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (recordButton), [this, sources] (int chosen) {
        if (chosen > 0 && chosen <= (int) sources.size())
            startCapture (sources[(size_t) chosen - 1].id);
    });
}

void MainComponent::startCapture (const juce::String& sourceId)
{
    juce::String error;
    captureSource = makeCaptureSource (sourceId, error);
    if (captureSource == nullptr || ! captureSource->start (recorder, error))
    {
        captureSource.reset();
        setStatus ("Cannot record: " + error);
        return;
    }
    lastCaptureSource = sourceId;
    engine.stop(); // the capture is what you hear now
    recordButton.setToggleState (true, juce::dontSendNotification);
    setStatus ("Recording " + captureSource->name() + ". Play your song, then press R or Stop.");
}

void MainComponent::stopCapture()
{
    if (captureSource != nullptr)
        captureSource->stop();
    captureSource.reset();
    juce::String error;
    auto captured = recorder.stop (error);
    recordButton.setToggleState (false, juce::dontSendNotification);
    recordButton.setButtonText ("Record");
    meter.level = 0.0f;
    meter.repaint();
    if (! captured)
    {
        setStatus ("Nothing recorded: " + error);
        return;
    }
    if (recorder.droppedSamples() > 0)
        setStatus ("Note: " + juce::String (recorder.droppedSamples()) + " samples were lost while recording");
    auto shared = std::make_shared<analysis::CaptureResult> (std::move (*captured));
    analyse ([shared] { analysis::ImportResult r; r.capture = std::move (*shared); return r; }, "the capture");
}

void MainComponent::LevelMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (gui::colours::outline);
    g.fillRoundedRectangle (r, 2.0f);
    const float h = r.getHeight() * juce::jlimit (0.0f, 1.0f, level);
    g.setColour (level > 0.98f ? juce::Colour (0xffd9534f) : gui::colours::source);
    g.fillRoundedRectangle (r.withTop (r.getBottom() - h), 2.0f);
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager(), 0, 2, 0, 2, false, false, true, false);
    selector->setSize (500, 400);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace jaudios
