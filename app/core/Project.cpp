#include "Project.h"

namespace jaudios
{

namespace
{
const std::pair<const char*, ChannelId> names[] = {
    { "source", ChannelId::source }, { "chords", ChannelId::chords }, { "bass", ChannelId::bass }, { "drums", ChannelId::drums },
    { "drums_stem", ChannelId::stemDrums }, { "bass_stem", ChannelId::stemBass }, { "other_stem", ChannelId::stemOther },
    { "vocals_stem", ChannelId::stemVocals },
};

juce::var numbers (const std::vector<double>& v)
{
    juce::Array<juce::var> a;
    for (double x : v)
        a.add (std::round (x * 1e6) / 1e6); // microseconds are plenty
    return a;
}

std::vector<double> numbersFrom (const juce::var& v)
{
    std::vector<double> out;
    if (const auto* a = v.getArray())
        for (const auto& x : *a)
            out.push_back ((double) x);
    return out;
}

bool anyStageOn (juce::AudioProcessorValueTreeState& s)
{
    for (const char* id : { "filter_on", "sat_on", "ott_on", "duck_on", "delay_on", "reverb_on" })
        if (s.getRawParameterValue (id)->load() >= 0.5f)
            return true;
    return false;
}
} // namespace

juce::String channelName (ChannelId id)
{
    for (const auto& [n, c] : names)
        if (c == id)
            return n;
    return {};
}

std::optional<ChannelId> channelFromName (juce::StringRef s)
{
    for (const auto& [n, c] : names)
        if (s == juce::StringRef (n))
            return c;
    return std::nullopt;
}

bool Project::save (const juce::File& f) const
{
    auto obj = [] { return new juce::DynamicObject(); };
    auto* root = obj();
    root->setProperty ("version", 2);

    auto* src = obj();
    src->setProperty ("file", source.file);
    src->setProperty ("sha256", source.sha256);
    src->setProperty ("sample_rate", source.sampleRate);
    src->setProperty ("num_samples", source.numSamples);
    root->setProperty ("source", juce::var (src));

    auto* an = obj();
    an->setProperty ("key", analysis.key);
    an->setProperty ("bpm", analysis.bpm);
    an->setProperty ("beats", numbers (analysis.beats));
    an->setProperty ("downbeats", numbers (analysis.downbeats));
    root->setProperty ("analysis", juce::var (an));

    auto* gen = obj();
    gen->setProperty ("style", generation.style);
    gen->setProperty ("progression", generation.progression);
    juce::Array<juce::var> chords;
    for (const auto& c : generation.chords)
        chords.add (c);
    gen->setProperty ("chords", chords);
    gen->setProperty ("bass_pattern", generation.bassPattern);
    gen->setProperty ("drum_pattern", generation.drumPattern);
    root->setProperty ("generation", juce::var (gen));

    auto* slots = obj();
    for (const auto& [channel, slot] : instruments)
        slots->setProperty (juce::String (channel), slot.toVar());
    root->setProperty ("slots", juce::var (slots)); // the name version 1 used

    auto* fxObj = obj();
    for (const auto& [channel, xml] : fx)
        fxObj->setProperty (juce::String (channel), xml);
    root->setProperty ("fx", juce::var (fxObj));

    juce::Array<juce::var> files;
    for (const auto& a : assets)
        files.add (a);
    root->setProperty ("assets", files);

    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (juce::var (root)));
}

std::optional<Project> Project::load (const juce::File& f, juce::String& error)
{
    const auto v = juce::JSON::parse (f);
    const int version = v.isObject() ? (int) v["version"] : 0;
    if (version != 1 && version != 2)
        return (error = f.getFileName() + " is not a JAudios project (version 1 or 2)", std::nullopt);
    Project p;
    const auto src = v["source"];
    p.source = { src["file"].toString(), src["sha256"].toString(), (double) src["sample_rate"], (juce::int64) src["num_samples"] };
    const auto an = v["analysis"];
    p.analysis.key = an["key"].toString();
    p.analysis.bpm = (double) an["bpm"];
    p.analysis.beats = numbersFrom (an["beats"]);
    p.analysis.downbeats = numbersFrom (an["downbeats"]);
    const auto gen = v["generation"];
    p.generation.style = gen["style"].toString();
    p.generation.progression = gen["progression"].toString();
    if (const auto* a = gen["chords"].getArray())
        for (const auto& c : *a)
            p.generation.chords.add (c.toString());
    p.generation.bassPattern = gen["bass_pattern"].toString();
    p.generation.drumPattern = gen["drum_pattern"].toString();
    if (const auto* o = v["slots"].getDynamicObject())
        for (const auto& prop : o->getProperties())
        {
            auto slot = InstrumentSlot::fromVar (prop.value, error);
            if (! slot)
                return (error = prop.name.toString() + ": " + error, std::nullopt);
            p.instruments[prop.name.toString().toStdString()] = std::move (*slot);
        }
    if (const auto* o = v["fx"].getDynamicObject())
        for (const auto& prop : o->getProperties())
        {
            if (! channelFromName (prop.name.toString()))
                return (error = "fx: unknown channel " + prop.name.toString(), std::nullopt);
            p.fx[prop.name.toString().toStdString()] = prop.value.toString();
        }
    if (const auto* a = v["assets"].getArray())
        for (const auto& x : *a)
            p.assets.add (x.toString());
    return p;
}

void Project::captureFx (AudioEngine& engine)
{
    fx.clear();
    for (const auto& [n, c] : names)
        if (auto* state = engine.fxState (c); state != nullptr && anyStageOn (*state))
            if (auto xml = state->copyState().createXml())
                fx[n] = xml->toString (juce::XmlElement::TextFormat().singleLine());
}

bool Project::applyFx (AudioEngine& engine) const
{
    for (const auto& [n, xmlText] : fx)
    {
        const auto c = channelFromName (n);
        auto* state = c ? engine.fxState (*c) : nullptr;
        auto xml = juce::parseXML (xmlText);
        if (state == nullptr || xml == nullptr || ! xml->hasTagName (state->state.getType()))
            return false;
        state->replaceState (juce::ValueTree::fromXml (*xml));
    }
    return true;
}

} // namespace jaudios
