#include "SongController.h"

#include "Project.h"

#include "engine/synth/SfzInstrument.h"

namespace jaudios
{

namespace gen = generators;

namespace
{
const juce::Identifier songId ("SONG"), chordsId ("CHORDS"), slotId ("SLOT"), mixerId ("MIXER"), channelId ("CHANNEL");
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
}

SongController::~SongController()
{
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
    if (t.hasType (channelId))
        applyMixer();
    else if (prop == styleProp || prop == progressionProp || t.hasType (slotId))
        rebuildParts();
}

void SongController::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (chordsId))
        rebuildParts();
}

void SongController::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (chordsId))
        rebuildParts();
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
    const double rate = analysed->sampleRate;
    engine.setChannelClip (ChannelId::chords, std::make_shared<const MidiClip> (makeClip (current.chords, rate)));
    engine.setChannelClip (ChannelId::bass, std::make_shared<const MidiClip> (makeClip (current.bass, rate)));
    engine.setChannelClip (ChannelId::drums, std::make_shared<const MidiClip> (makeClip (synth::drumsToMidi (current.drumNotes), rate)));
    if (onPartsChanged)
        onPartsChanged();
}

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

juce::File SongController::exportWav (const juce::String& partName, const juce::File& dir, double tailSeconds) const
{
    const auto part = findExportPart (partName, analysed->stems.has_value());
    if (! part)
        return {};
    // a second engine set up like the live one (the live one keeps playing)
    AudioEngine offline;
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

    const auto audio = exporting::alignToGrid (renderPart (offline, *part, sourceBuffer->getNumSamples(), sr, tailSeconds), sr, barGrid);
    const auto f = dir.getChildFile (exportFileStem (songName(), part->name, theory::traditionalName (analysed->source.key), barGrid.bpm()) + ".wav");
    return exporting::writeWav (f, audio, sr, exporting::WavFormat::int24) ? f : juce::File();
}

} // namespace jaudios
