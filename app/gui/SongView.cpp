#include "SongView.h"

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace jaudios::gui
{

SongView::SongView()
{
    const std::pair<const char*, juce::Colour> names[] = {
        { "Source", colours::source }, { "Chords", colours::chords }, { "Bass", colours::bass }, { "Drums", colours::drums },
        { "Drums stem", colours::source }, { "Bass stem", colours::source }, { "Other stem", colours::source }, { "Vocals stem", colours::source },
    };
    for (int c = 0; c < numChannelIds; ++c)
    {
        auto h = std::make_unique<ChannelHeader> ((ChannelId) c, names[c].first, names[c].second);
        h->onShowFx = [this] (ChannelId id) { showFx (id); };
        addChildComponent (*h);
        headers.push_back (std::move (h));
    }
    for (auto* lane : std::initializer_list<juce::Component*> { &source, &chords, &bass, &drums })
        addAndMakeVisible (lane);
    addAndMakeVisible (overlay);
    addAndMakeVisible (tiles);
    chords.onChordClicked = [this] (int slot) { showAlternatives (slot); };
    source.onSeek = [this] (double t) {
        if (song != nullptr)
            song->audioEngine().seek ((juce::int64) (t * song->song().sampleRate));
        overlay.setPosition (t);
    };
    tiles.onStatus = [this] (const juce::String& s) { if (onStatus) onStatus (s); };
    bass.onStatus = drums.onStatus = [this] (const juce::String& s) { if (onStatus) onStatus (s); };
    setWantsKeyboardFocus (true);
    startTimerHz (60);
}

SongView::~SongView()
{
    stopTimer();
    if (song != nullptr)
    {
        song->onPartsChanged = nullptr;
        song->onMixerChanged = nullptr;
    }
}

ChannelHeader& SongView::header (ChannelId id) { return *headers[(size_t) id]; }

void SongView::setSong (SongController* s, WaveformPeaks peaks, const juce::File& dragFolder)
{
    if (song != nullptr)
    {
        song->onPartsChanged = nullptr;
        song->onMixerChanged = nullptr;
    }
    song = s;
    for (auto* lane : std::initializer_list<Lane*> { &source, &chords, &bass, &drums })
        lane->setSong (song);
    source.setPeaks (std::move (peaks));
    for (auto& h : headers)
        h->setSong (song);
    const bool stems = song != nullptr && song->song().stems.has_value();
    for (int c = 0; c < numChannelIds; ++c)
        headers[(size_t) c]->setVisible (c < (int) ChannelId::stemDrums || stems);
    tiles.setSong (song, dragFolder);
    model.setDuration (song != nullptr ? song->durationSeconds() : 1.0);
    overlay.setPosition (0.0);
    if (song != nullptr)
    {
        song->onPartsChanged = [this] {
            chords.repaint();
            bass.repaint();
            drums.repaint();
            if (onSongChanged)
                onSongChanged();
        };
        song->onMixerChanged = [this] {
            for (auto& h : headers)
                h->refresh();
            if (onSongChanged)
                onSongChanged();
        };
    }
    resized();
    repaint();
}

juce::Rectangle<int> SongView::lanesArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedRight (tilesWidth);
}

void SongView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    if (song == nullptr)
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText ("Drop an audio file here, or Open... a song to get chords, a bassline and drums that fit it",
                          lanesArea().reduced (40), juce::Justification::centred, 3);
    }
}

void SongView::resized()
{
    auto r = getLocalBounds();
    tiles.setBounds (r.removeFromRight (tilesWidth));
    auto left = r.removeFromLeft (headerWidth);
    const auto lanes = r;
    model.setWidth (lanes.getWidth());

    auto place = [&] (juce::Component& lane, ChannelHeader* h, int height) {
        lane.setBounds (r.removeFromTop (height));
        if (h != nullptr)
            h->setBounds (left.removeFromTop (height));
        else
            left.removeFromTop (height);
    };
    place (source, &header (ChannelId::source), sourceHeight);
    const bool stems = song != nullptr && song->song().stems.has_value();
    if (stems)
    {
        // the four stem strips side by side under the source
        auto row = r.removeFromTop (stemsHeight);
        left.removeFromTop (stemsHeight);
        const int w = row.getWidth() / 4;
        for (int s = 0; s < 4; ++s)
            header ((ChannelId) ((int) ChannelId::stemDrums + s)).setBounds (row.removeFromLeft (s == 3 ? row.getWidth() : w));
    }
    place (chords, &header (ChannelId::chords), chordHeight);
    place (bass, &header (ChannelId::bass), bassHeight);
    place (drums, &header (ChannelId::drums), drumHeight);
    overlay.setBounds (lanes.withBottom (drums.getBottom()));
}

void SongView::timerCallback()
{
    if (song == nullptr)
        return;
    auto& engine = song->audioEngine();
    auto& player = engine.getSourcePlayer();
    player.collectGarbage();
    const double t = (double) player.getPosition() / song->song().sampleRate;
    if (player.isPlaying())
        model.reveal (t);
    overlay.setPosition (t);
}

void SongView::togglePlay()
{
    if (song == nullptr)
        return;
    auto& engine = song->audioEngine();
    if (engine.getSourcePlayer().isPlaying())
        engine.stop();
    else
        engine.play();
}

bool SongView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }
    if ((key.getKeyCode() == 'R' || key.getKeyCode() == 'r') && onRecordKey) // with or without a song
    {
        onRecordKey();
        return true;
    }
    if (song == nullptr)
        return false;
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress ('z', cmd, 0))
        return song->undoManager().undo(), true;
    if (key == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress ('y', cmd, 0))
        return song->undoManager().redo(), true;
    if (key == juce::KeyPress ('0', cmd, 0))
        return model.showAll(), true;
    return false;
}

void SongView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float x = (float) (e.getEventRelativeTo (this).x - headerWidth);
    if (e.mods.isCommandDown())
        model.zoom (std::pow (2.0, (double) wheel.deltaY * 2.0), x); // Cmd+scroll zooms around the pointer
    else
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        model.scrollBy (-(double) delta * model.length() * 0.5);
    }
}

void SongView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    model.zoom (scaleFactor, (float) (e.getEventRelativeTo (this).x - headerWidth)); // trackpad pinch
}

std::unique_ptr<AlternativesPanel> SongView::makeAlternatives (int slot)
{
    if (song == nullptr || slot < 0 || slot >= (int) song->currentProgression().chords.size())
        return nullptr;
    return std::make_unique<AlternativesPanel> (*song, slot);
}

void SongView::showAlternatives (int slot)
{
    auto panel = makeAlternatives (slot);
    if (panel == nullptr)
        return;
    auto* raw = panel.get();
    auto& box = juce::CallOutBox::launchAsynchronously (std::move (panel), getLocalArea (&chords, chords.slotBounds (slot)), this);
    raw->onChosen = [&box] { box.dismiss(); };
}

void SongView::showFx (ChannelId id)
{
    if (song == nullptr)
        return;
    auto* state = song->audioEngine().fxState (id);
    if (state == nullptr)
        return;
    juce::DialogWindow::LaunchOptions options;
    auto editor = std::make_unique<juce::GenericAudioProcessorEditor> (state->processor);
    editor->setSize (420, 520);
    options.content.setOwned (editor.release());
    options.dialogTitle = state->processor.getName() + " - insert FX";
    options.dialogBackgroundColour = colours::panel;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

} // namespace jaudios::gui
