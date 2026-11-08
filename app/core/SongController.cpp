#include "SongController.h"

#include "Project.h"

#include "engine/synth/SfzInstrument.h"

namespace jaudios
{

namespace gen = generators;

namespace
{
const juce::Identifier songId ("SONG"), chordsId ("CHORDS"), slotId ("SLOT"), mixerId ("MIXER"), channelId ("CHANNEL"),
    editsId ("EDITS"), drumHitId ("DRUM"), bassNoteId ("BASS");
const juce::Identifier elementProp ("element"), barProp ("bar"), stepProp ("step"), velocityProp ("velocity");
const juce::Identifier styleProp ("style"), progressionProp ("progression"), slotProp ("slot"), rootProp ("root"),
    qualityProp ("quality"), inversionProp ("inversion"), idProp ("id"), gainProp ("gain"), muteProp ("mute"), soloProp ("solo");

float defaultGain (ChannelId c)
{
    switch (c)
    {
        case ChannelId::source: return -6.0f;
        case ChannelId::chords: return -12.0f;
        case ChannelId::bass:   return -9.0f;
        case ChannelId::drums:  return -10.0f;
        default:                return 0.0f;
    }
}
} // namespace

std::string defaultChordsPreset (gen::Style style)
{
    return style == gen::Style::pop || style == gen::Style::cinematic ? "piano"
         : style == gen::Style::lofi || style == gen::Style::rnb ? "keys"
         : style == gen::Style::trance ? "pluck" : "pad";
}

std::string defaultBassPreset (gen::Style style)
{
    return style == gen::Style::techno || style == gen::Style::trance ? "saw_bass" : "sub_bass";
}

SongController::SongController (AudioEngine& e, std::shared_ptr<const SongAnalysis> s, std::shared_ptr<const juce::AudioBuffer<float>> source,
                                gen::Style style)
    : engine (e),
      analysed (std::make_shared<SongAnalysis> (*s)), // ours: a style change re-suggests into it
      sourceBuffer (std::move (source)),
      barGrid (exporting::BarGrid::fromAnalysis (analysed->analysis.rhythm.beats, analysed->analysis.rhythm.downbeats,
                                                 analysed->analysis.rhythm.bpm)),
      suggestedFor (style),
      tree (songId)
{
    tree.setProperty (styleProp, (int) style, nullptr);
    tree.setProperty (progressionProp, 0, nullptr);
    tree.appendChild (juce::ValueTree (chordsId), nullptr);
    juce::ValueTree mixer (mixerId);
    for (int c = 0; c < numChannelIds; ++c)
    {
        juce::ValueTree ch (channelId);
        ch.setProperty (idProp, c, nullptr);
        ch.setProperty (gainProp, defaultGain ((ChannelId) c), nullptr);
        ch.setProperty (muteProp, false, nullptr);
        ch.setProperty (soloProp, false, nullptr);
        mixer.appendChild (ch, nullptr);
    }
    tree.appendChild (mixer, nullptr);
    tree.appendChild (juce::ValueTree (editsId), nullptr);

    applyMixer();
    engine.setTempo (analysed->analysis.rhythm.bpm > 0.0 ? analysed->analysis.rhythm.bpm : 120.0);
    engine.setSource (sourceBuffer);
    if (analysed->stems && std::abs (analysed->stems->sampleRate - analysed->sampleRate) < 0.5)
        engine.setStems (std::make_shared<const juce::AudioBuffer<float>> (analysis::stems::packStems (*analysed->stems)));
    if (analysed->progressions.empty())
    {
        gen::EngineSettings settings;
        settings.style = style;
        suggestProgressions (*analysed, settings);
    }
    setInstrumentsForStyle();
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<synth::DrumInstrument>());
    rebuildParts();
    tree.addListener (this);
    for (int c = 0; c < numChannelIds; ++c) // FX edits change what an export contains
        if (auto* fx = engine.fxState ((ChannelId) c))
            for (const auto& spec : fx::paramSpecs())
                fx->addParameterListener (spec.id, this);
}

SongController::~SongController()
{
    for (auto& j : jobs)
        j->cancel();
    exportThread.removeAllJobs (true, 30000);
    for (int c = 0; c < numChannelIds; ++c)
        if (auto* fx = engine.fxState ((ChannelId) c))
            for (const auto& spec : fx::paramSpecs())
                fx->removeParameterListener (spec.id, this);
    tree.removeListener (this);
}

//==============================================================================
gen::Style SongController::style() const { return (gen::Style) (int) tree[styleProp]; }
int SongController::progressionIndex() const { return (int) tree[progressionProp]; }
juce::ValueTree SongController::chordsNode() const { return tree.getChildWithName (chordsId); }
juce::ValueTree SongController::channelNode (ChannelId c) const { return tree.getChildWithName (mixerId).getChild ((int) c); }

void SongController::setStyle (gen::Style s)
{
    if (s == style())
        return;
    undo.beginNewTransaction ("Style");
    chordsNode().removeAllChildren (&undo);
    editsNode().removeAllChildren (&undo); // another groove: the steps mean other hits
    tree.setProperty (progressionProp, 0, &undo);
    tree.setProperty (styleProp, (int) s, &undo);
}

void SongController::selectProgression (int index)
{
    index = juce::jlimit (0, std::max (0, numProgressions() - 1), index);
    if (index == progressionIndex() && chordsNode().getNumChildren() == 0)
        return;
    undo.beginNewTransaction ("Progression");
    chordsNode().removeAllChildren (&undo);
    tree.setProperty (progressionProp, index, &undo);
}

std::vector<gen::Alternative> SongController::alternativesFor (int slot) const
{
    if (slot < 0 || slot >= (int) current.progression.chords.size())
        return {};
    gen::EngineSettings settings;
    settings.style = style();
    return gen::alternatives (analysed->source, current.progression, slot, settings);
}

void SongController::replaceChord (int slot, const theory::Chord& chord)
{
    if (slot < 0 || slot >= (int) current.progression.chords.size() || current.progression.chords[(size_t) slot] == chord)
        return;
    undo.beginNewTransaction ("Chord");
    auto chords = chordsNode();
    auto node = chords.getChildWithProperty (slotProp, slot);
    if (! node.isValid())
    {
        node = juce::ValueTree (slotId);
        node.setProperty (slotProp, slot, nullptr);
        node.setProperty (rootProp, chord.root, nullptr);
        node.setProperty (qualityProp, (int) chord.quality, nullptr);
        node.setProperty (inversionProp, chord.inversion, nullptr);
        chords.appendChild (node, &undo);
        return;
    }
    node.setProperty (rootProp, chord.root, &undo);
    node.setProperty (qualityProp, (int) chord.quality, &undo);
    node.setProperty (inversionProp, chord.inversion, &undo);
}

void SongController::resetChord (int slot)
{
    auto node = chordsNode().getChildWithProperty (slotProp, slot);
    if (! node.isValid())
        return;
    undo.beginNewTransaction ("Reset chord");
    chordsNode().removeChild (node, &undo);
}

bool SongController::isReplaced (int slot) const { return chordsNode().getChildWithProperty (slotProp, slot).isValid(); }

float SongController::gainDecibels (ChannelId c) const { return (float) channelNode (c)[gainProp]; }
bool SongController::isMuted (ChannelId c) const { return (bool) channelNode (c)[muteProp]; }
bool SongController::isSoloed (ChannelId c) const { return (bool) channelNode (c)[soloProp]; }

void SongController::setGainDecibels (ChannelId c, float dB)
{
    // a fader drag is one undo step: coalesce consecutive gain changes of the same channel
    if (undo.getUndoDescription() != "Gain " + juce::String ((int) c) || undo.getNumActionsInCurrentTransaction() == 0)
        undo.beginNewTransaction ("Gain " + juce::String ((int) c));
    channelNode (c).setProperty (gainProp, juce::jlimit (-60.0f, 12.0f, dB), &undo);
}

void SongController::setMute (ChannelId c, bool m)
{
    undo.beginNewTransaction ("Mute");
    channelNode (c).setProperty (muteProp, m, &undo);
}

void SongController::setSolo (ChannelId c, bool s)
{
    undo.beginNewTransaction ("Solo");
    channelNode (c).setProperty (soloProp, s, &undo);
}

//==============================================================================
void SongController::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& prop)
{
    ++rev;
    if (t.hasType (channelId))
        applyMixer();
    else if (t.hasType (drumHitId) || t.hasType (bassNoteId))
        applyAccentEdits();
    else if (prop == styleProp || prop == progressionProp || t.hasType (slotId))
        rebuildParts();
}

void SongController::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    ++rev;
    if (parent.hasType (chordsId))
        rebuildParts();
    else if (parent.hasType (editsId))
        applyAccentEdits();
}

void SongController::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    ++rev;
    if (parent.hasType (chordsId))
        rebuildParts();
    else if (parent.hasType (editsId))
        applyAccentEdits();
}

void SongController::setInstrumentsForStyle()
{
    const auto s = style();
    const auto chords = defaultChordsPreset (s), bass = defaultBassPreset (s);
    if (chords != chordsPreset)
        if (auto* p = synth::findSfzPreset (chords))
        {
            auto inst = std::make_shared<synth::SfzInstrument>();
            if (inst->load (*p) && engine.setChannelInstrument (ChannelId::chords, inst))
                chordsPreset = chords;
        }
    if (bass != bassPreset)
        if (auto* p = synth::findSfzPreset (bass))
        {
            auto inst = std::make_shared<synth::SfzInstrument>();
            if (inst->load (*p) && engine.setChannelInstrument (ChannelId::bass, inst))
                bassPreset = bass;
        }
}

void SongController::rebuildParts()
{
    const auto s = style();
    if (s != suggestedFor)
    {
        gen::EngineSettings settings;
        settings.style = s;
        suggestProgressions (*analysed, settings);
        suggestedFor = s;
        setInstrumentsForStyle();
    }
    if (analysed->progressions.empty())
        return;
    auto progression = analysed->progressions[(size_t) juce::jlimit (0, numProgressions() - 1, progressionIndex())];
    for (const auto& node : chordsNode())
    {
        const int slot = node[slotProp];
        if (slot >= 0 && slot < (int) progression.chords.size())
            progression.chords[(size_t) slot] = theory::Chord { (int) node[rootProp], (theory::Quality) (int) node[qualityProp], (int) node[inversionProp] };
    }
    current = generateParts (*analysed, progression, s, true);
    generatedDrums = current.drumNotes;
    generatedBass = current.bassNotes;
    engine.setChannelClip (ChannelId::chords, std::make_shared<const MidiClip> (makeClip (current.chords, analysed->sampleRate)));
    applyAccentEdits(); // bass + drums clips, with the user's accents
}

void SongController::applyAccentEdits()
{
    // drums: from the generated groove, edits by (element, bar, step)
    current.drumNotes = generatedDrums;
    const auto bars = songBars (*analysed);
    const double swing = current.drumPattern != nullptr ? current.drumPattern->swing : 0.0;
    for (const auto& e : editsNode())
    {
        const int velocity = e[velocityProp];
        if (e.hasType (drumHitId))
        {
            const DrumHit h { (gen::DrumElement) (int) e[elementProp], (int) e[barProp], (int) e[stepProp] };
            auto it = std::find_if (current.drumNotes.begin(), current.drumNotes.end(), [&] (const gen::DrumNote& n) {
                return n.element == h.element && n.bar == h.bar && n.step == h.step;
            });
            if (it != current.drumNotes.end())
            {
                if (velocity > 0)
                    it->velocity = velocity;
                else
                    current.drumNotes.erase (it);
            }
            else if (velocity > 0 && h.bar >= 0 && h.bar < (int) bars.size())
            {
                // an added hit, placed like the generator places one (swing on odd steps)
                const auto [b0, b1] = bars[(size_t) h.bar];
                const double stepLen = (b1 - b0) / gen::DrumPattern::stepsPerBar;
                const double start = b0 + h.step * stepLen + (h.step % 2 == 1 ? swing * stepLen : 0.0);
                const bool longOne = h.element == gen::DrumElement::openhat || h.element == gen::DrumElement::ride;
                current.drumNotes.push_back ({ start, start + (longOne ? 2.0 : 0.9) * stepLen, h.element, velocity, h.bar, h.step });
            }
        }
    }
    std::stable_sort (current.drumNotes.begin(), current.drumNotes.end(),
                      [] (const gen::DrumNote& a, const gen::DrumNote& b) { return a.startSec < b.startSec; });

    // bass: from the generated line, edits by 1/16 step
    current.bassNotes = generatedBass;
    for (const auto& e : editsNode())
        if (e.hasType (bassNoteId))
        {
            const int step = e[stepProp], velocity = e[velocityProp];
            auto it = std::find_if (current.bassNotes.begin(), current.bassNotes.end(),
                                    [&] (const gen::BassNote& n) { return bassStepOf (n) == step; });
            if (it == current.bassNotes.end())
                continue;
            if (velocity > 0)
                it->velocity = velocity;
            else
                current.bassNotes.erase (it);
        }
    current.bass.clear();
    for (const auto& n : current.bassNotes)
    {
        current.bass.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity).withTimeStamp (n.startSec));
        current.bass.addEvent (juce::MidiMessage::noteOff (1, n.pitch).withTimeStamp (n.endSec));
    }
    current.bass.sort();
    current.bass.updateMatchedPairs();

    const double rate = analysed->sampleRate;
    engine.setChannelClip (ChannelId::bass, std::make_shared<const MidiClip> (makeClip (current.bass, rate)));
    engine.setChannelClip (ChannelId::drums, std::make_shared<const MidiClip> (makeClip (synth::drumsToMidi (current.drumNotes), rate)));
    if (onPartsChanged)
        onPartsChanged();
}

//==============================================================================
juce::ValueTree SongController::editsNode() const { return tree.getChildWithName (editsId); }

juce::ValueTree SongController::drumEdit (DrumHit h) const
{
    for (const auto& e : editsNode())
        if (e.hasType (drumHitId) && (int) e[elementProp] == (int) h.element && (int) e[barProp] == h.bar && (int) e[stepProp] == h.step)
            return e;
    return {};
}

juce::ValueTree SongController::bassEdit (int step) const
{
    for (const auto& e : editsNode())
        if (e.hasType (bassNoteId) && (int) e[stepProp] == step)
            return e;
    return {};
}

void SongController::setEdit (juce::ValueTree existing, std::function<juce::ValueTree()> make, int velocity, const juce::String& undoName,
                              bool coalesce)
{
    // a drag on one note is one undo step: coalesce consecutive edits with the same name; a click is its own
    if (! coalesce || undo.getUndoDescription() != undoName || undo.getNumActionsInCurrentTransaction() == 0)
        undo.beginNewTransaction (undoName);
    velocity = juce::jlimit (0, 127, velocity);
    if (existing.isValid())
        existing.setProperty (velocityProp, velocity, &undo);
    else
    {
        auto node = make();
        node.setProperty (velocityProp, velocity, nullptr);
        editsNode().appendChild (node, &undo);
    }
}

int SongController::generatedDrumVelocity (DrumHit h) const
{
    for (const auto& n : generatedDrums)
        if (n.element == h.element && n.bar == h.bar && n.step == h.step)
            return n.velocity;
    return 0;
}

int SongController::drumVelocity (DrumHit h) const
{
    if (const auto e = drumEdit (h); e.isValid())
        return (int) e[velocityProp];
    return generatedDrumVelocity (h);
}

bool SongController::isDrumHitEdited (DrumHit h) const { return drumEdit (h).isValid(); }

void SongController::setDrumVelocity (DrumHit h, int velocity) { writeDrum (h, velocity, true); }

void SongController::writeDrum (DrumHit h, int velocity, bool coalesce)
{
    if (h.bar < 0 || h.bar >= (int) analysed->source.bars.size() || h.step < 0 || h.step >= gen::DrumPattern::stepsPerBar)
        return;
    const auto undoName = "Drum " + juce::String ((int) h.element) + ":" + juce::String (h.bar) + ":" + juce::String (h.step);
    setEdit (drumEdit (h), [h] {
        juce::ValueTree n (drumHitId);
        n.setProperty (elementProp, (int) h.element, nullptr);
        n.setProperty (barProp, h.bar, nullptr);
        n.setProperty (stepProp, h.step, nullptr);
        return n;
    }, velocity, undoName, coalesce);
}

void SongController::resetDrumHit (DrumHit h)
{
    if (auto e = drumEdit (h); e.isValid())
    {
        undo.beginNewTransaction ("Reset drum hit");
        editsNode().removeChild (e, &undo);
    }
}

void SongController::cycleDrumAccent (DrumHit h)
{
    const int base = generatedDrumVelocity (h);
    const int now = drumVelocity (h);
    const bool edited = isDrumHitEdited (h);
    auto remove = [&] {
        undo.beginNewTransaction ("Accent");
        editsNode().removeChild (drumEdit (h), &undo);
    };
    if (base == 0) // a step the groove leaves empty: add -> accent -> ghost -> remove
    {
        if (! edited || now == 0)
            writeDrum (h, addedHitVelocity, false);
        else if (now == addedHitVelocity)
            writeDrum (h, accentOf (addedHitVelocity), false);
        else if (now == accentOf (addedHitVelocity))
            writeDrum (h, ghostOf (addedHitVelocity), false);
        else
            remove();
        return;
    }
    if (! edited)
        writeDrum (h, accentOf (base), false);
    else if (now == accentOf (base))
        writeDrum (h, ghostOf (base), false);
    else if (now == ghostOf (base))
        writeDrum (h, 0, false);
    else
        remove(); // off (or dragged) -> back to the groove
}

int SongController::bassStepOf (const gen::BassNote& n) const
{
    return (int) std::lround (barGrid.toBeats (n.startSec) * 4.0);
}

int SongController::generatedBassVelocity (int step) const
{
    for (const auto& n : generatedBass)
        if (bassStepOf (n) == step)
            return n.velocity;
    return 0;
}

int SongController::bassVelocity (int step) const
{
    if (const auto e = bassEdit (step); e.isValid())
        return (int) e[velocityProp];
    return generatedBassVelocity (step);
}

bool SongController::isBassNoteEdited (int step) const { return bassEdit (step).isValid(); }

void SongController::setBassVelocity (int step, int velocity) { writeBass (step, velocity, true); }

void SongController::writeBass (int step, int velocity, bool coalesce)
{
    if (generatedBassVelocity (step) == 0)
        return; // the bassline's rhythm comes from its pattern: only its notes are edited
    setEdit (bassEdit (step), [step] {
        juce::ValueTree n (bassNoteId);
        n.setProperty (stepProp, step, nullptr);
        return n;
    }, velocity, "Bass " + juce::String (step), coalesce);
}

void SongController::resetBassNote (int step)
{
    if (auto e = bassEdit (step); e.isValid())
    {
        undo.beginNewTransaction ("Reset bass note");
        editsNode().removeChild (e, &undo);
    }
}

void SongController::cycleBassAccent (int step)
{
    const int base = generatedBassVelocity (step);
    if (base == 0)
        return;
    const int now = bassVelocity (step);
    if (! isBassNoteEdited (step))
        writeBass (step, accentOf (base), false);
    else if (now == accentOf (base))
        writeBass (step, ghostOf (base), false);
    else if (now == ghostOf (base))
        writeBass (step, 0, false);
    else
    {
        undo.beginNewTransaction ("Accent");
        editsNode().removeChild (bassEdit (step), &undo);
    }
}

void SongController::clearAccents()
{
    if (editsNode().getNumChildren() == 0)
        return;
    undo.beginNewTransaction ("Clear accents");
    editsNode().removeAllChildren (&undo);
}

int SongController::numAccentEdits() const { return editsNode().getNumChildren(); }

void SongController::applyMixer()
{
    for (int c = 0; c < numChannelIds; ++c)
    {
        const auto id = (ChannelId) c;
        engine.setChannelGainDecibels (id, gainDecibels (id));
        engine.setChannelMute (id, isMuted (id));
        engine.setChannelSolo (id, isSoloed (id));
    }
    if (onMixerChanged)
        onMixerChanged();
}

//==============================================================================
juce::File SongController::exportMidi (const juce::String& part, const juce::File& dir) const
{
    namespace ex = exporting;
    std::vector<ex::MidiPart> parts;
    if (part == "chords" || part == "all")
        parts.push_back ({ "Chords", current.chords });
    if (part == "bass" || part == "all")
        parts.push_back ({ "Bass", current.bass });
    if (part == "drums" || part == "all")
        parts.push_back ({ "Drums", synth::drumsToMidi (current.drumNotes) });
    if (parts.empty())
        return {};
    const auto key = analysed->source.key;
    const auto label = part == "all" ? juce::String ("All parts") : parts.front().name;
    const auto f = dir.getChildFile (exportFileStem (songName(), label, theory::traditionalName (key), barGrid.bpm()) + ".mid");
    return ex::writeMidiFile (f, ex::makeMidiFile (songName(), parts, barGrid, key)) ? f : juce::File();
}

std::shared_ptr<WavExportJob> SongController::startWavExport (const juce::String& partName, const juce::File& dir, double tailSeconds)
{
    const auto part = findExportPart (partName, analysed->stems.has_value());
    if (! part)
        return nullptr;
    // finished jobs' engines go here, on the message thread (an engine owns a device manager)
    jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [] (const auto& j) { return j->state() != WavExportJob::State::rendering; }),
                jobs.end());

    auto job = std::make_shared<WavExportJob>();
    job->part = part->name;
    job->songRevision = rev.load();
    job->engine = std::make_unique<AudioEngine>();
    auto& offline = *job->engine;
    // a second engine set up like the live one (which keeps playing)
    for (int c = 0; c < numChannelIds; ++c)
    {
        const auto id = (ChannelId) c;
        offline.setChannelGainDecibels (id, gainDecibels (id));
        if (! part->channel) // the mix as heard; a single part is rendered whatever the mute says
            offline.setChannelMute (id, isMuted (id));
        if (auto* from = engine.fxState (id))
            offline.fxState (id)->replaceState (from->copyState());
    }
    offline.setTempo (barGrid.bpm());
    const double sr = analysed->sampleRate;
    offline.prepareOffline (sr, 512);
    offline.setSource (sourceBuffer);
    if (analysed->stems && std::abs (analysed->stems->sampleRate - sr) < 0.5)
        offline.setStems (std::make_shared<const juce::AudioBuffer<float>> (analysis::stems::packStems (*analysed->stems)));
    for (const auto& [channel, preset] : { std::pair { ChannelId::chords, chordsPreset }, std::pair { ChannelId::bass, bassPreset } })
        if (auto* p = synth::findSfzPreset (preset))
        {
            auto inst = std::make_shared<synth::SfzInstrument>();
            if (inst->load (*p))
                offline.setChannelInstrument (channel, inst);
        }
    offline.setChannelInstrument (ChannelId::drums, std::make_shared<synth::DrumInstrument>());
    offline.setChannelClip (ChannelId::chords, std::make_shared<const MidiClip> (makeClip (current.chords, sr)));
    offline.setChannelClip (ChannelId::bass, std::make_shared<const MidiClip> (makeClip (current.bass, sr)));
    offline.setChannelClip (ChannelId::drums, std::make_shared<const MidiClip> (makeClip (synth::drumsToMidi (current.drumNotes), sr)));

    const auto file = dir.getChildFile (exportFileStem (songName(), part->name, theory::traditionalName (analysed->source.key), barGrid.bpm()) + ".wav");
    const auto length = (juce::int64) sourceBuffer->getNumSamples();
    const auto grid = barGrid;
    auto* raw = job.get(); // the controller keeps the job alive until the thread is done with it
    exportThread.addJob ([raw, part = *part, file, length, sr, grid, tailSeconds] {
        auto audio = renderPart (*raw->engine, part, length, sr, tailSeconds, 512, [raw] (double f) {
            raw->fraction.store (f);
            return ! raw->cancelRequested.load();
        });
        if (raw->cancelRequested.load())
        {
            raw->status.store (WavExportJob::State::cancelled);
            return;
        }
        const bool ok = exporting::writeWav (file, exporting::alignToGrid (audio, sr, grid), sr, exporting::WavFormat::int24);
        raw->result = ok ? file : juce::File();
        raw->fraction.store (1.0);
        raw->status.store (ok ? WavExportJob::State::done : WavExportJob::State::failed);
    });
    jobs.push_back (job);
    return job;
}

juce::File SongController::exportWav (const juce::String& partName, const juce::File& dir, double tailSeconds)
{
    auto job = startWavExport (partName, dir, tailSeconds);
    if (job == nullptr)
        return {};
    while (job->state() == WavExportJob::State::rendering)
        juce::Thread::sleep (5);
    return job->state() == WavExportJob::State::done ? job->file() : juce::File();
}

} // namespace jaudios
