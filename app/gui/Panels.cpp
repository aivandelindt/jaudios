#include "Panels.h"

#include "ExternalDrag.h"
#include "Theme.h"

namespace jaudios::gui
{

ChannelHeader::ChannelHeader (ChannelId i, const juce::String& n, juce::Colour c) : id (i), name (n), colour (c)
{
    for (auto* b : { &mute, &solo, &fx })
        addAndMakeVisible (b);
    addAndMakeVisible (gain);
    mute.setClickingTogglesState (true);
    solo.setClickingTogglesState (true);
    mute.setColour (juce::TextButton::buttonOnColourId, colours::mute);
    solo.setColour (juce::TextButton::buttonOnColourId, colours::solo);
    mute.setTooltip ("Mute");
    solo.setTooltip ("Solo");
    fx.setTooltip ("Insert effects");
    gain.setRange (-60.0, 12.0, 0.1);
    gain.setSkewFactorFromMidPoint (-12.0);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTextValueSuffix (" dB");
    gain.setPopupDisplayEnabled (true, true, nullptr);
    gain.setColour (juce::Slider::thumbColourId, colour);
    mute.onClick = [this] { if (song != nullptr) song->setMute (id, mute.getToggleState()); };
    solo.onClick = [this] { if (song != nullptr) song->setSolo (id, solo.getToggleState()); };
    gain.onValueChange = [this] { if (song != nullptr) song->setGainDecibels (id, (float) gain.getValue()); };
    fx.onClick = [this] { if (onShowFx) onShowFx (id); };
}

void ChannelHeader::setSong (SongController* s)
{
    song = s;
    refresh();
}

void ChannelHeader::refresh()
{
    const bool on = song != nullptr;
    for (juce::Component* c : { (juce::Component*) &mute, (juce::Component*) &solo, (juce::Component*) &fx, (juce::Component*) &gain })
        c->setEnabled (on);
    if (! on)
        return;
    mute.setToggleState (song->isMuted (id), juce::dontSendNotification);
    solo.setToggleState (song->isSoloed (id), juce::dontSendNotification);
    gain.setValue (song->gainDecibels (id), juce::dontSendNotification);
}

void ChannelHeader::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colour);
    g.fillRect (0, 0, 3, getHeight());
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (name, 10, 4, getWidth() - 14, 16, juce::Justification::left);
    g.setColour (colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void ChannelHeader::resized()
{
    auto r = getLocalBounds().reduced (8, 4).withTrimmedLeft (2);
    r.removeFromTop (18);
    auto row = r.removeFromTop (22);
    mute.setBounds (row.removeFromLeft (26));
    row.removeFromLeft (3);
    solo.setBounds (row.removeFromLeft (26));
    row.removeFromLeft (3);
    fx.setBounds (row.removeFromRight (32));
    row.removeFromRight (3);
    gain.setBounds (row);
}

//==============================================================================
void PianoKeys::setNotes (std::vector<int> pcs)
{
    notes = std::move (pcs);
    repaint();
}

void PianoKeys::paint (juce::Graphics& g)
{
    static constexpr bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    const int whites = 14;
    const float w = (float) getWidth() / whites, h = (float) getHeight();
    // voice the chord upwards from its root in the first octave, as rendered
    std::vector<int> voiced;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        int n = notes[i];
        while (! voiced.empty() && n <= voiced.back())
            n += 12;
        voiced.push_back (n);
    }
    auto lit = [&] (int k) { return std::find (voiced.begin(), voiced.end(), k) != voiced.end(); };
    int white = 0;
    for (int k = 0; k < 24; ++k)
        if (! black[k % 12])
        {
            const juce::Rectangle<float> r ((float) white * w, 0.0f, w - 1.0f, h);
            g.setColour (lit (k) ? colours::chords : juce::Colour (0xffe8eaee));
            g.fillRect (r);
            ++white;
        }
    white = 0;
    for (int k = 0; k < 24; ++k)
    {
        if (! black[k % 12])
        {
            ++white;
            continue;
        }
        const juce::Rectangle<float> r ((float) white * w - w * 0.3f, 0.0f, w * 0.6f, h * 0.6f);
        g.setColour (lit (k) ? colours::chords.darker (0.2f) : juce::Colour (0xff20232a));
        g.fillRect (r);
    }
}

//==============================================================================
AlternativesPanel::AlternativesPanel (SongController& s, int sl) : song (s), slot (sl), alts (s.alternativesFor (sl))
{
    addAndMakeVisible (piano);
    addAndMakeVisible (reset);
    reset.setEnabled (song.isReplaced (slot));
    reset.onClick = [this] { choose (-1); };
    piano.setNotes (song.currentProgression().chords[(size_t) slot].pitchClasses());
    setSize (300, headerHeight + rowHeight * std::max (1, (int) alts.size()) + 70 + 34);
}

void AlternativesPanel::choose (int row)
{
    if (row < 0)
        song.resetChord (slot);
    else if (row < (int) alts.size())
        song.replaceChord (slot, alts[(size_t) row].chord);
    if (onChosen)
        onChosen();
}

int AlternativesPanel::rowAt (int y) const
{
    const int row = (y - headerHeight) / rowHeight;
    return y >= headerHeight && row < (int) alts.size() ? row : -1;
}

void AlternativesPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    const auto key = song.song().source.key;
    const auto& current = song.currentProgression().chords[(size_t) slot];
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Bar slot " + juce::String (slot + 1) + ": " + current.name (key), 10, 6, getWidth() - 20, 18, juce::Justification::left);
    if (alts.empty())
    {
        g.setColour (colours::textDim);
        g.drawText ("No alternatives fit here", 10, headerHeight, getWidth() - 20, rowHeight, juce::Justification::left);
    }
    const double best = alts.empty() ? 0.0 : alts.front().score;
    for (int i = 0; i < (int) alts.size(); ++i)
    {
        const juce::Rectangle<int> r (6, headerHeight + i * rowHeight, getWidth() - 12, rowHeight - 2);
        g.setColour (i == hover ? colours::chords.withAlpha (0.25f) : colours::lane);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (alts[(size_t) i].chord.name (key), r.withTrimmedLeft (8).withWidth (70), juce::Justification::centredLeft);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (alts[(size_t) i].roman, r.withTrimmedLeft (84).withWidth (60), juce::Justification::centredLeft);
        // how it fits, relative to the best alternative (a short bar)
        const float fit = (float) juce::jlimit (0.0, 1.0, 1.0 + (alts[(size_t) i].score - best) * 2.0);
        g.setColour (colours::chords.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> ((float) r.getRight() - 90.0f, (float) r.getCentreY() - 3.0f, 80.0f * fit, 6.0f));
    }
}

void AlternativesPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    reset.setBounds (r.removeFromBottom (26));
    r.removeFromBottom (6);
    piano.setBounds (r.removeFromBottom (64));
}

void AlternativesPanel::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row == hover)
        return;
    hover = row;
    piano.setNotes (row >= 0 ? alts[(size_t) row].chord.pitchClasses() : song.currentProgression().chords[(size_t) slot].pitchClasses());
    repaint();
}

void AlternativesPanel::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    piano.setNotes (song.currentProgression().chords[(size_t) slot].pitchClasses());
    repaint();
}

void AlternativesPanel::mouseDown (const juce::MouseEvent& e)
{
    if (const int row = rowAt (e.y); row >= 0)
        choose (row);
}

//==============================================================================
ExportTiles::ExportTiles() = default;

void ExportTiles::setSong (SongController* s, const juce::File& dragFolder)
{
    for (auto& [i, j] : jobs)
        j->cancel();
    jobs.clear();
    song = s;
    folder = dragFolder;
    tileList.clear();
    if (song != nullptr)
    {
        tileList.push_back ({ "mix", "Mix", "wav" });
        for (const char* p : { "chords", "bass", "drums" })
        {
            const juce::String label = juce::String (p).substring (0, 1).toUpperCase() + juce::String (p).substring (1);
            tileList.push_back ({ p, label, "midi" });
            tileList.push_back ({ p, label, "wav" });
        }
        tileList.push_back ({ "all", "All parts", "midi" });
        if (song->song().stems)
            for (const char* p : { "drums_stem", "bass_stem", "other_stem", "vocals_stem" })
                tileList.push_back ({ p, juce::String (p).upToFirstOccurrenceOf ("_", false, false).substring (0, 1).toUpperCase()
                                             + juce::String (p).upToFirstOccurrenceOf ("_", false, false).substring (1) + " stem", "wav" });
    }
    repaint();
}

juce::Rectangle<int> ExportTiles::tileBounds (int index) const
{
    const int columns = 2, h = 44, gap = 6;
    const int w = (getWidth() - gap * (columns + 1)) / columns;
    return { gap + (index % columns) * (w + gap), 28 + (index / columns) * (h + gap), w, h };
}

int ExportTiles::tileAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) tileList.size(); ++i)
        if (tileBounds (i).contains (p))
            return i;
    return -1;
}

juce::File ExportTiles::fileFor (int index)
{
    if (song == nullptr || index < 0 || index >= (int) tileList.size())
        return {};
    const auto& t = tileList[(size_t) index];
    if (t.kind == "midi")
        return song->exportMidi (t.part, folder);
    return wavState (index) == WavState::ready ? jobs[index]->file() : juce::File();
}

std::shared_ptr<WavExportJob> ExportTiles::job (int index) const
{
    const auto it = jobs.find (index);
    return it != jobs.end() ? it->second : nullptr;
}

ExportTiles::WavState ExportTiles::wavState (int index) const
{
    const auto j = job (index);
    if (j == nullptr || song == nullptr)
        return WavState::none;
    switch (j->state())
    {
        case WavExportJob::State::rendering: return WavState::rendering;
        case WavExportJob::State::done:      return j->revision() == song->revision() ? WavState::ready : WavState::outOfDate;
        case WavExportJob::State::failed:    return WavState::failed;
        case WavExportJob::State::cancelled: return WavState::none;
    }
    return WavState::none;
}

void ExportTiles::clickTile (int index)
{
    if (song == nullptr || index < 0 || index >= (int) tileList.size() || tileList[(size_t) index].kind != "wav")
        return;
    if (wavState (index) == WavState::rendering)
    {
        jobs[index]->cancel();
        if (onStatus)
            onStatus ("Cancelled");
    }
    else if (wavState (index) != WavState::ready)
    {
        jobs[index] = song->startWavExport (tileList[(size_t) index].part, folder);
        if (onStatus)
            onStatus ("Rendering " + tileList[(size_t) index].label + "... drag the tile when it is ready");
        startTimerHz (15);
    }
    repaint();
}

void ExportTiles::timerCallback()
{
    bool any = false;
    for (const auto& [i, j] : jobs)
        any = any || j->state() == WavExportJob::State::rendering;
    repaint();
    if (! any)
        stopTimer();
}

void ExportTiles::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("DRAG INTO YOUR DAW", 8, 6, getWidth() - 16, 16, juce::Justification::left);
    for (int i = 0; i < (int) tileList.size(); ++i)
    {
        const auto& t = tileList[(size_t) i];
        const auto r = tileBounds (i).toFloat();
        const auto c = t.part.startsWith ("chords") ? colours::chords : t.part.startsWith ("bass") ? colours::bass
                     : t.part.startsWith ("drums") ? colours::drums : colours::source;
        g.setColour (i == pressed ? c.withAlpha (0.35f) : colours::lane);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (c.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (t.label, r.reduced (8, 4).removeFromTop (r.getHeight() / 2), juce::Justification::centredLeft);
        juce::String sub = "MIDI";
        if (t.kind == "wav")
            switch (wavState (i))
            {
                case WavState::none:      sub = "Click: render WAV"; break;
                case WavState::rendering: sub = "WAV " + juce::String (juce::roundToInt (job (i)->progress() * 100.0)) + " %"; break;
                case WavState::ready:     sub = "WAV ready: drag"; break;
                case WavState::outOfDate: sub = "Changed: click"; break;
                case WavState::failed:    sub = "Failed: retry"; break;
            }
        g.setColour (t.kind == "wav" && wavState (i) == WavState::ready ? c : colours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (sub, r.reduced (8, 4).removeFromBottom (r.getHeight() / 2 - 4), juce::Justification::centredLeft);
        if (t.kind == "wav" && wavState (i) == WavState::rendering)
        {
            const auto bar = r.reduced (6.0f, 0.0f).removeFromBottom (3.0f);
            g.setColour (c.withAlpha (0.25f));
            g.fillRect (bar);
            g.setColour (c);
            g.fillRect (bar.withWidth (bar.getWidth() * (float) job (i)->progress()));
        }
    }
}

void ExportTiles::mouseDown (const juce::MouseEvent& e)
{
    pressed = tileAt (e.getPosition());
    dragStarted = false;
    repaint();
}

void ExportTiles::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed < 0 || dragStarted || e.getDistanceFromDragStart() < 6)
        return;
    dragStarted = true;
    const auto& t = tileList[(size_t) pressed];
    if (t.kind == "wav" && wavState (pressed) != WavState::ready)
    {
        if (wavState (pressed) != WavState::rendering)
            clickTile (pressed); // start it; the drag can follow once it is ready
        return;
    }
    const auto f = fileFor (pressed);
    if (! f.existsAsFile())
    {
        if (onStatus)
            onStatus ("Could not write the file");
        return;
    }
    if (onStatus)
        onStatus ("Drop " + f.getFileName() + " into your DAW");
    dragFilesOut ({ f.getFullPathName() }, *this);
}

void ExportTiles::mouseUp (const juce::MouseEvent& e)
{
    if (! dragStarted && pressed >= 0 && tileAt (e.getPosition()) == pressed)
        clickTile (pressed);
    pressed = -1;
    repaint();
}

} // namespace jaudios::gui
