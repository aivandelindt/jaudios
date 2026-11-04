#include "MainComponent.h"

namespace jaudios
{

MainComponent::MainComponent()
{
    for (auto* b : { &settingsButton, &openButton, &playButton, &stopButton })
        addAndMakeVisible (b);

    addAndMakeVisible (waveform);
    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::topLeft);

    settingsButton.onClick = [this] { showAudioSettings(); };
    openButton.onClick = [this] { showOpenDialog(); };
    playButton.onClick = [this] { engine.getSourcePlayer().play(); };
    stopButton.onClick = [this] { engine.getSourcePlayer().stop(); };
    waveform.onSeek = [this] (double t) { engine.getSourcePlayer().seek ((juce::int64) (t * analysis::analysisSampleRate)); };

    deviceError = engine.openDefaultDevice (AudioEngine::defaultBlockSize);

    setSize (1000, 560);
    startTimerHz (30);
    updateStatus();
}

MainComponent::~MainComponent()
{
    stopTimer();
    analysisService.cancelAll();
    engine.closeDevice();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    if (dragHover)
    {
        g.setColour (juce::Colour (0xff5fb3c9));
        g.drawRect (waveform.getBounds().expanded (3), 2);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto row = area.removeFromTop (32);

    for (auto* b : { &openButton, &playButton, &stopButton })
    {
        b->setBounds (row.removeFromLeft (90));
        row.removeFromLeft (8);
    }
    settingsButton.setBounds (row.removeFromRight (150));

    area.removeFromTop (10);
    waveform.setBounds (area.removeFromTop (200));
    area.removeFromTop (10);
    statusLabel.setBounds (area);
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
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), importer.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  importFile (fc.getResult());
                          });
}

void MainComponent::importFile (const juce::File& file)
{
    if (importing)
        return;

    importing = true;
    importStatus = "Importing " + file.getFileName() + "...";
    updateStatus();

    analysisService.submit<ImportJobResult> (
        [this, file]
        {
            ImportJobResult r;
            r.import = importer.importFile (file);
            if (r.import.ok())
                r.peaks = WaveformPeaks::compute (r.import.capture->buffer, r.import.capture->sampleRate);
            return r;
        },
        [this] (ImportJobResult r) { onImported (std::move (r)); });
}

void MainComponent::onImported (ImportJobResult r)
{
    importing = false;

    if (! r.import.ok())
    {
        importStatus = "Import failed: " + r.import.error;
        updateStatus();
        return;
    }

    auto& c = *r.import.capture;
    importStatus = c.sourceFile.getFileName() + " - " + c.formatName + ", "
                 + juce::String (c.originalSampleRate, 0) + " Hz -> " + juce::String (c.sampleRate, 0) + " Hz, "
                 + juce::String (c.originalNumChannels) + " ch, " + juce::String (c.durationSeconds(), 2) + " s";

    engine.getSourcePlayer().stop();
    engine.getSourcePlayer().setSource (std::make_shared<const juce::AudioBuffer<float>> (std::move (c.buffer)));
    waveform.setPeaks (std::move (r.peaks));
    updateStatus();
}

void MainComponent::timerCallback()
{
    auto& player = engine.getSourcePlayer();
    player.collectGarbage();
    waveform.setPlayheadSec ((double) player.getPosition() / analysis::analysisSampleRate);
}

void MainComponent::updateStatus()
{
    juce::String text;

    if (deviceError.isNotEmpty())
        text << "Audio device: " << deviceError << "\n";
    else if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
        text << "Audio device: " << device->getName() << " @ " << device->getCurrentSampleRate() << " Hz, "
             << device->getCurrentBufferSizeSamples() << " samples, output latency "
             << juce::String (engine.getOutputLatencyMs(), 2) << " ms\n";

    if (importStatus.isNotEmpty())
        text << importStatus << "\n";

    statusLabel.setText (text, juce::dontSendNotification);
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 2, 0, 2, false, false, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace jaudios
