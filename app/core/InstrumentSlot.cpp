#include "InstrumentSlot.h"

namespace jaudios
{

namespace
{
const char* kindName (InstrumentSlot::Kind k)
{
    switch (k)
    {
        case InstrumentSlot::Kind::builtin: return "builtin";
        case InstrumentSlot::Kind::sfzFile: return "sfz_file";
        case InstrumentSlot::Kind::drums:   return "drums";
        case InstrumentSlot::Kind::plugin:  return "plugin";
    }
    return "?";
}
} // namespace

juce::var InstrumentSlot::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", kindName (kind));
    switch (kind)
    {
        case Kind::builtin: o->setProperty ("preset", juce::String (preset)); break;
        case Kind::sfzFile: o->setProperty ("file", file.getFullPathName()); break;
        case Kind::drums: break;
        case Kind::plugin:
            if (auto xml = plugin.createXml())
                o->setProperty ("plugin", xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()));
            o->setProperty ("name", plugin.name);
            o->setProperty ("state", state.toBase64Encoding());
            break;
    }
    return juce::var (o);
}

std::optional<InstrumentSlot> InstrumentSlot::fromVar (const juce::var& v, juce::String& error)
{
    InstrumentSlot s;
    const auto kind = v["kind"].toString();
    if (kind == "builtin")
    {
        s.kind = Kind::builtin;
        s.preset = v["preset"].toString().toStdString();
        if (synth::findSfzPreset (s.preset) == nullptr)
            return (error = "unknown built-in preset '" + juce::String (s.preset) + "'", std::nullopt);
    }
    else if (kind == "sfz_file")
    {
        s.kind = Kind::sfzFile;
        s.file = juce::File (v["file"].toString());
    }
    else if (kind == "drums")
        s.kind = Kind::drums;
    else if (kind == "plugin")
    {
        s.kind = Kind::plugin;
        auto xml = juce::XmlDocument::parse (v["plugin"].toString());
        if (xml == nullptr || ! s.plugin.loadFromXml (*xml))
            return (error = "unreadable plugin description", std::nullopt);
        if (! s.state.fromBase64Encoding (v["state"].toString()))
            return (error = "unreadable plugin state", std::nullopt);
    }
    else
        return (error = "unknown slot kind '" + kind + "'", std::nullopt);
    return s;
}

std::optional<InstrumentSlot> captureSlot (synth::Instrument& inst)
{
    InstrumentSlot s;
    if (auto* sfz = dynamic_cast<synth::SfzInstrument*> (&inst))
    {
        if (! sfz->presetId().empty())
        {
            s.kind = InstrumentSlot::Kind::builtin;
            s.preset = sfz->presetId();
        }
        else
        {
            s.kind = InstrumentSlot::Kind::sfzFile;
            s.file = sfz->sfzFile();
        }
        return s;
    }
    if (dynamic_cast<synth::DrumInstrument*> (&inst) != nullptr)
    {
        s.kind = InstrumentSlot::Kind::drums;
        return s;
    }
    if (auto* p = dynamic_cast<PluginInstrument*> (&inst))
    {
        s.kind = InstrumentSlot::Kind::plugin;
        s.plugin = p->description();
        s.state = p->getState();
        return s;
    }
    return std::nullopt;
}

std::shared_ptr<synth::Instrument> instantiate (const InstrumentSlot& s, PluginHost& host, double sr, int block, juce::String& error)
{
    switch (s.kind)
    {
        case InstrumentSlot::Kind::builtin:
        {
            auto i = std::make_shared<synth::SfzInstrument>();
            const auto* preset = synth::findSfzPreset (s.preset);
            if (preset == nullptr || ! i->load (*preset))
                return (error = "could not load preset " + juce::String (s.preset), nullptr);
            return i;
        }
        case InstrumentSlot::Kind::sfzFile:
        {
            auto i = std::make_shared<synth::SfzInstrument>();
            if (! i->loadFile (s.file))
                return (error = "could not load " + s.file.getFullPathName(), nullptr);
            return i;
        }
        case InstrumentSlot::Kind::drums:
            return std::make_shared<synth::DrumInstrument>();
        case InstrumentSlot::Kind::plugin:
        {
            auto i = host.createInstrument (s.plugin, sr, block, error);
            if (i == nullptr)
                return (error = "could not load " + s.plugin.name + ": " + error, nullptr);
            if (s.state.getSize() > 0)
                i->setState (s.state);
            return i;
        }
    }
    return nullptr;
}

bool ProjectInstruments::save (const juce::File& f) const
{
    auto* slotsObj = new juce::DynamicObject();
    for (const auto& [channel, slot] : slots)
        slotsObj->setProperty (juce::String (channel), slot.toVar());
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("slots", juce::var (slotsObj));
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (juce::var (root)));
}

std::optional<ProjectInstruments> ProjectInstruments::load (const juce::File& f, juce::String& error)
{
    const auto v = juce::JSON::parse (f);
    if (! v.isObject() || (int) v["version"] != 1)
        return (error = f.getFileName() + " is not a JAudios project (version 1)", std::nullopt);
    ProjectInstruments p;
    if (const auto* o = v["slots"].getDynamicObject())
        for (const auto& prop : o->getProperties())
        {
            auto slot = InstrumentSlot::fromVar (prop.value, error);
            if (! slot)
                return (error = prop.name.toString() + ": " + error, std::nullopt);
            p.slots[prop.name.toString().toStdString()] = std::move (*slot);
        }
    return p;
}

} // namespace jaudios
