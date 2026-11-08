// ui-shell: the editor's components, headless. The shared timeline (zoom around the pointer,
// scroll), the song view's layout and painting (snapshot: every lane draws its part), chord
// alternatives through the popup, keys (Space, Cmd+Z), mixer headers, export tiles, and the
// 60 fps budget for a full repaint.
#include "TestSignals.h"

#include "app/gui/SongView.h"
#include "app/gui/Theme.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>

using namespace jaudios;
using namespace jaudios::gui;
namespace gen = jaudios::generators;
using Catch::Matchers::WithinAbs;
using theory::Chord;
using theory::Quality;

namespace
{
constexpr double sr = 44100.0;

std::shared_ptr<SongAnalysis> syntheticSong (int bars = 8)
{
    auto s = std::make_shared<SongAnalysis>();
    s->sampleRate = sr;
    s->durationSec = 2.0 * bars;
    auto& src = s->source;
    src.key = { 0, theory::Mode::major };
    src.keyMargin = 0.6;
    const std::vector<Chord> chords { { 0, Quality::maj }, { 7, Quality::maj }, { 9, Quality::min }, { 5, Quality::maj } };
    for (int b = 0; b < bars; ++b)
    {
        const auto& c = chords[(size_t) b % 4];
        const double t0 = 2.0 * b;
        gen::BarEvidence e { t0, t0 + 2.0, std::array<float, 12> {} };
        const auto pcs = c.pitchClasses();
        for (size_t k = 0; k < pcs.size(); ++k)
        {
            (*e.chroma)[(size_t) pcs[k]] = k == 0 ? 1.0f : 0.8f;
            src.melody.push_back ({ t0 + 0.5 * (double) k, t0 + 0.5 * (double) k + 0.45, 60 + pcs[k], 0.9 });
        }
        src.bass.push_back ({ t0, t0 + 1.8, 36 + c.bassPitchClass(), 0.9 });
        src.bars.push_back (e);
    }
    auto& r = s->analysis.rhythm;
    r.bpm = 120.0;
    for (int i = 0; i < 4 * bars; ++i)
    {
        r.beats.push_back (0.5 * i);
        s->kicks.push_back (0.5 * i);
        if (i % 4 == 0)
            r.downbeats.push_back (0.5 * i);
    }
    gen::EngineSettings settings;
    suggestProgressions (*s, settings);
    return s;
}

struct Fixture
{
    AudioEngine engine;
    std::shared_ptr<juce::AudioBuffer<float>> source;
    std::unique_ptr<SongController> song;
    SongView view;
    test::TempDir tmp;
    explicit Fixture (int bars = 8)
        : source (test::makeSine (2, (int) (2.0 * bars * sr), 110.0, sr, 0.4f))
    {
        engine.prepareOffline (sr, 512);
        song = std::make_unique<SongController> (engine, syntheticSong (bars), source, gen::Style::pop);
        view.setSize (1280, 520);
        view.setSong (song.get(), WaveformPeaks::compute (*source, sr), tmp.dir);
    }
};

/** Share of pixels in `area` close to `c` (any brightness of that hue counts as "drawn"). */
double coverage (const juce::Image& img, juce::Rectangle<int> area, juce::Colour c)
{
    int hits = 0, total = 0;
    for (int y = area.getY(); y < area.getBottom(); y += 2)
        for (int x = area.getX(); x < area.getRight(); x += 2)
        {
            const auto p = img.getPixelAt (x, y);
            ++total;
            if (std::abs (p.getHue() - c.getHue()) < 0.04f && p.getSaturation() > 0.25f && p.getBrightness() > 0.2f)
                ++hits;
        }
    return total > 0 ? (double) hits / total : 0.0;
}
} // namespace

TEST_CASE ("Timeline: zoom keeps the time under the pointer, scroll and limits", "[ui][timeline]")
{
    TimelineModel t;
    t.setWidth (1000);
    t.setDuration (100.0);
    CHECK_THAT (t.toSeconds (500.0f), WithinAbs (50.0, 1e-9));
    CHECK_THAT (t.toX (25.0), WithinAbs (250.0, 1e-4));
    const double under = t.toSeconds (300.0f);
    t.zoom (4.0, 300.0f);
    CHECK_THAT (t.length(), WithinAbs (25.0, 1e-9));
    CHECK_THAT (t.toSeconds (300.0f), WithinAbs (under, 1e-9)); // the anchor stays put
    t.scrollBy (1000.0);
    CHECK_THAT (t.start() + t.length(), WithinAbs (100.0, 1e-9)); // clamped at the end
    t.zoom (1e6, 0.0f);
    CHECK_THAT (t.length(), WithinAbs (TimelineModel::minLength, 1e-9));
    t.zoom (1e-6, 0.0f);
    CHECK_THAT (t.length(), WithinAbs (100.0, 1e-9));
    t.zoom (10.0, 0.0f);
    t.reveal (80.0); // the playhead ran off the right edge: page on
    CHECK (t.start() <= 80.0);
    CHECK (t.start() + t.length() >= 80.0);
}

TEST_CASE ("Song view paints every lane with its part (snapshot)", "[ui][view]")
{
    Fixture f;
    const auto img = f.view.createComponentSnapshot (f.view.getLocalBounds(), true, 1.0f);
    if (const auto out = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_UI_SNAPSHOT", {}); out.isNotEmpty())
    {
        juce::FileOutputStream os { juce::File (out) };
        os.setPosition (0);
        os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }
    const int x0 = SongView::headerWidth, x1 = f.view.getWidth() - SongView::tilesWidth;
    int y = 0;
    auto band = [&] (int h) { const juce::Rectangle<int> r (x0, y, x1 - x0, h); y += h; return r; };
    const auto sourceArea = band (SongView::sourceHeight), chordArea = band (SongView::chordHeight),
               bassArea = band (SongView::bassHeight), drumArea = band (SongView::drumHeight);
    CHECK (coverage (img, sourceArea, colours::source) > 0.2);   // the waveform (a steady sine)
    CHECK (coverage (img, chordArea, colours::chords) > 0.05);   // chord blocks
    CHECK (coverage (img, bassArea, colours::bass) > 0.01);      // bass notes (thin rows: the pitch range spans the lane)
    CHECK (coverage (img, drumArea, colours::drums) > 0.01);     // drum hits
    CHECK (coverage (img, { f.view.getWidth() - SongView::tilesWidth, 0, SongView::tilesWidth, 300 }, colours::chords) > 0.0);
}

TEST_CASE ("Chord lane: slots map to the bars; the alternatives panel replaces a chord; Cmd+Z undoes it", "[ui][view]")
{
    Fixture f;
    auto& lane = f.view.chordLane();
    const auto& slots = f.song->currentProgression().slots;
    REQUIRE (slots.size() == 8);
    const float x = f.view.timeline().toX ((slots[2].startSec + slots[2].endSec) / 2);
    CHECK (lane.slotAt (x) == 2);
    CHECK (lane.slotBounds (2).getCentreX() == juce::roundToInt (x));

    int clicked = -1;
    lane.onChordClicked = [&] (int s) { clicked = s; }; // what a click does (the popup needs a window)
    lane.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x, 10.0f }, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &lane, &lane, juce::Time::getCurrentTime(), { x, 10.0f }, juce::Time::getCurrentTime(), 1, false));
    CHECK (clicked == 2);

    auto panel = f.view.makeAlternatives (2);
    REQUIRE (panel != nullptr);
    REQUIRE (panel->numRows() > 0);
    if (const auto out = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_UI_SNAPSHOT", {}); out.isNotEmpty())
    {
        juce::FileOutputStream os { juce::File (out).getSiblingFile ("alternatives.png") };
        os.setPosition (0);
        os.truncate();
        juce::PNGImageFormat().writeImageToStream (panel->createComponentSnapshot (panel->getLocalBounds(), true, 2.0f), os);
    }
    const auto before = f.song->currentProgression().chords[2];
    bool closed = false;
    panel->onChosen = [&] { closed = true; };
    panel->choose (0);
    CHECK (closed);
    CHECK (f.song->isReplaced (2));
    CHECK (f.song->currentProgression().chords[2] != before);
    CHECK (f.view.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
    CHECK_FALSE (f.song->isReplaced (2));
    CHECK (f.view.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK (f.song->isReplaced (2));
    auto again = f.view.makeAlternatives (2);
    again->choose (-1); // back to the suggestion
    CHECK_FALSE (f.song->isReplaced (2));
}

TEST_CASE ("Space plays and stops; the playhead follows the transport", "[ui][view]")
{
    Fixture f;
    juce::AudioBuffer<float> block (2, 512);
    CHECK (f.view.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    for (int i = 0; i < 20; ++i)
        f.engine.renderBlock (block.getArrayOfWritePointers(), 2, 512);
    CHECK (f.engine.getSourcePlayer().isPlaying());
    CHECK (f.view.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
    f.engine.renderBlock (block.getArrayOfWritePointers(), 2, 512);
    CHECK_FALSE (f.engine.getSourcePlayer().isPlaying());
    const double pos = (double) f.engine.getSourcePlayer().getPosition() / sr;
    CHECK (pos > 0.2);
    f.view.playhead().setPosition (pos);
    CHECK_THAT (f.view.playhead().position(), WithinAbs (pos, 1e-12));
}

TEST_CASE ("Cmd+scroll and pinch zoom around the pointer; plain scroll pans", "[ui][view]")
{
    Fixture f (32);
    auto& t = f.view.timeline();
    const float px = 300.0f;
    const double under = t.toSeconds (px);
    auto event = [&] (juce::ModifierKeys mods) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { px + SongView::headerWidth, 50.0f }, mods, 1.0f, 0.0f, 0.0f,
                                 0.0f, 0.0f, &f.view, &f.view, juce::Time::getCurrentTime(), { px + SongView::headerWidth, 50.0f },
                                 juce::Time::getCurrentTime(), 1, false);
    };
    juce::MouseWheelDetails wheel {};
    wheel.deltaY = 0.5f;
    f.view.mouseWheelMove (event (juce::ModifierKeys::commandModifier), wheel);
    CHECK (t.length() < 64.0 * 0.6);
    CHECK_THAT (t.toSeconds (px), WithinAbs (under, 1e-6));
    const double len = t.length();
    f.view.mouseMagnify (event ({}), 2.0f);
    CHECK_THAT (t.length(), WithinAbs (len / 2.0, 1e-9));
    const double start = t.start();
    wheel.deltaY = -0.2f;
    f.view.mouseWheelMove (event ({}), wheel);
    CHECK (t.start() > start);
    CHECK (f.view.keyPressed (juce::KeyPress ('0', juce::ModifierKeys::commandModifier, 0)));
    CHECK_THAT (t.length(), WithinAbs (64.0, 1e-9));
}

TEST_CASE ("Channel headers drive the mixer and follow undo", "[ui][view]")
{
    Fixture f;
    auto& h = f.view.header (ChannelId::bass);
    // a click: the toggle flips, then the button's handler runs (triggerClick would need a message loop)
    h.muteButton().setToggleState (true, juce::dontSendNotification);
    h.muteButton().onClick();
    CHECK (f.song->isMuted (ChannelId::bass));
    REQUIRE (f.song->undoManager().undo());
    CHECK_FALSE (f.song->isMuted (ChannelId::bass));
    CHECK_FALSE (h.muteButton().getToggleState()); // refreshed from the controller
    h.gainSlider().setValue (-3.0, juce::sendNotificationSync); // a drag
    CHECK_THAT (f.song->gainDecibels (ChannelId::bass), WithinAbs (-3.0, 1e-4));
    CHECK_FALSE (f.view.header (ChannelId::stemDrums).isVisible()); // no stems in this song
}

TEST_CASE ("Export tiles write DAW-ready files into the drag folder", "[ui][view][export]")
{
    Fixture f;
    auto& tiles = f.view.exportTiles();
    REQUIRE (tiles.tiles().size() == 8); // mix wav; chords/bass/drums midi + wav; all midi
    for (int i = 0; i < (int) tiles.tiles().size(); ++i)
    {
        const auto r = tiles.tileBounds (i);
        CHECK (tiles.tileAt (r.getCentre()) == i);
    }
    int midiTile = -1;
    for (int i = 0; i < (int) tiles.tiles().size(); ++i)
        if (tiles.tiles()[(size_t) i].part == "bass" && tiles.tiles()[(size_t) i].kind == "midi")
            midiTile = i;
    REQUIRE (midiTile >= 0);
    const auto f1 = tiles.fileFor (midiTile);
    CHECK (f1.existsAsFile());
    CHECK (f1.getParentDirectory() == f.tmp.dir);
    CHECK (f1.getFileExtension() == ".mid");
}

TEST_CASE ("A full repaint of a 3-minute song fits the 60 fps budget", "[ui][view][perf]")
{
#if JUCE_DEBUG
    SKIP ("timing is only meaningful in an optimised build");
#endif
    Fixture f (90); // 180 s
    juce::Image img (juce::Image::ARGB, f.view.getWidth(), f.view.getHeight(), true);
    {
        juce::Graphics warm (img); // font and glyph caches, as after the first frame on screen
        f.view.paintEntireComponent (warm, true);
    }
    std::vector<double> times;
    for (int i = 0; i < 10; ++i)
    {
        juce::Graphics g (img);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        f.view.paintEntireComponent (g, true);
        times.push_back (juce::Time::getMillisecondCounterHiRes() - t0);
        f.view.timeline().zoom (1.3, 400.0f);
    }
    std::sort (times.begin(), times.end());
    const double median = times[times.size() / 2];
    WARN ("full repaint: median " << median << " ms, worst " << times.back() << " ms");
    // the typical frame (the median; a single slow frame is the scheduler, e.g. under ctest -j)
    CHECK (median < 16.7);
}
