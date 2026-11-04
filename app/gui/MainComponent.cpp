#include "MainComponent.h"

namespace jaudios
{

MainComponent::MainComponent()
{
    for (auto* b : { &settingsButton, &playButton, &stopButton })
        addAndMakeVisible (b);

    addAndMakeVisible (statusLabel);
    statusLabel.setJustificationType (juce::Justification::topLeft);

    settingsButton.onClick = [this] { showAudioSettings(); };
    playButton.onClick = [this] { engine.getSourcePlayer().play(); };
    stopButton.onClick = [this] { engine.getSourcePlayer().stop(); };

    deviceError = engine.openDefaultDevice (AudioEngine::defaultBlockSize);

    setSize (900, 520);
    startTimerHz (20);
    updateStatus();
}

MainComponent::~MainComponent()
{
    stopTimer();
    analysis.cancelAll();
    engine.closeDevice();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto row = area.removeFromTop (32);

    settingsButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (8);
    playButton.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    stopButton.setBounds (row.removeFromLeft (80));

    area.removeFromTop (8);
    statusLabel.setBounds (area);
}

void MainComponent::timerCallback()
{
    engine.getSourcePlayer().collectGarbage();
    updateStatus();
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
