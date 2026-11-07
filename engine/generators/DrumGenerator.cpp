#include "DrumGenerator.h"

#include <algorithm>
#include <cmath>

namespace jaudios::generators
{

namespace
{
const char* const elementNames[numDrumElements] = { "kick", "snare", "clap", "hat", "openhat", "perc", "ride" };
const char* const variantNames[numDrumVariants] = { "fill", "riser_hit", "drop", "half_time" };

bool validLane (const juce::String& s)
{
    return s.containsOnly ("Xxog.");
}

int velocityOf (char c) noexcept
{
    switch (c)
    {
        case 'X': return 118;
        case 'x': return 100;
        case 'o': return 78;
        case 'g': return 45;
        default:  return 0;
    }
}
} // namespace

const char* elementName (DrumElement e) noexcept { return elementNames[(int) e]; }

std::optional<DrumElement> elementFromName (const juce::String& n)
{
    for (int e = 0; e < numDrumElements; ++e)
        if (n.trim().equalsIgnoreCase (elementNames[e]))
            return (DrumElement) e;
    return std::nullopt;
}

int gmNote (DrumElement e) noexcept
{
    static const int notes[numDrumElements] = { 36, 38, 39, 42, 46, 37, 51 };
    return notes[(int) e];
}

int minIntensity (DrumElement e) noexcept
{
    static const int levels[numDrumElements] = { 1, 1, 1, 2, 3, 4, 5 };
    return levels[(int) e];
}

const char* variantName (DrumVariant v) noexcept { return variantNames[(int) v]; }

std::optional<DrumVariant> variantFromName (const juce::String& n)
{
    for (int v = 0; v < numDrumVariants; ++v)
        if (n.trim().equalsIgnoreCase (variantNames[v]) || n.trim().replaceCharacter ('-', '_').equalsIgnoreCase (variantNames[v]))
            return (DrumVariant) v;
    return std::nullopt;
}

std::optional<DrumPattern> DrumPattern::fromJson (const juce::var& v, juce::String& error)
{
    // Mirrors drums/schema.json and tools/validate_patterns.py (semantic_drums).
    auto fail = [&] (const juce::String& e) { error = e; return std::optional<DrumPattern> {}; };
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail ("not a JSON object");
    static const juce::StringArray keys { "name", "style", "description", "bars", "swing", "lanes", "fill", "riser_hit", "drop", "half_time" };
    for (const auto& p : obj->getProperties())
        if (! keys.contains (p.name.toString()))
            return fail ("unexpected property '" + p.name.toString() + "'");

    DrumPattern p;
    p.name = v["name"].toString().toStdString();
    if (p.name.empty() || ! juce::String (p.name).containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789_"))
        return fail ("name must be lower-case letters, digits and _");
    p.style = v["style"].toString().toStdString();
    if (! styleFromName (p.style) || p.style != styleName (*styleFromName (p.style)))
        return fail ("unknown style '" + juce::String (p.style) + "'");
    p.description = v["description"].toString().toStdString();
    if (! v["bars"].isInt() || (int) v["bars"] < 2 || (int) v["bars"] > 8)
        return fail ("bars must be 2 .. 8");
    p.bars = v["bars"];
    if (obj->hasProperty ("swing"))
    {
        p.swing = v["swing"];
        if (p.swing < 0.0 || p.swing > 0.75)
            return fail ("swing must be 0 .. 0.75");
    }

    auto readLanes = [&] (const juce::var& lanes, const char* where, int expected, bool overlay,
                          std::array<std::optional<std::string>, numDrumElements>& out) -> bool {
        const auto* lo = lanes.getDynamicObject();
        if (lo == nullptr)
            return (error = juce::String (where) + " must be an object", false);
        for (const auto& prop : lo->getProperties())
        {
            const auto el = elementFromName (prop.name.toString());
            if (! el || prop.name.toString() != elementNames[(int) *el])
                return (error = juce::String (where) + ": unknown element '" + prop.name.toString() + "'", false);
            const auto s = prop.value.toString();
            if (! prop.value.isString() || ! validLane (s))
                return (error = juce::String (where) + "." + prop.name.toString() + ": only X x o g . allowed", false);
            if (s.length() != expected && ! (overlay && s.isEmpty()))
                return (error = juce::String (where) + "." + prop.name.toString() + ": " + juce::String (s.length())
                              + " steps, expected " + juce::String (expected), false);
            out[(size_t) *el] = s.toStdString();
        }
        return true;
    };

    std::array<std::optional<std::string>, numDrumElements> main {};
    if (! readLanes (v["lanes"], "lanes", p.bars * stepsPerBar, false, main))
        return std::nullopt;
    bool any = false;
    for (int e = 0; e < numDrumElements; ++e)
    {
        p.lanes[(size_t) e] = main[(size_t) e].value_or ("");
        any = any || p.lanes[(size_t) e].find_first_not_of ('.') != std::string::npos;
    }
    if (! any)
        return fail ("lanes: no hits");
    for (int vi = 0; vi < numDrumVariants; ++vi)
        if (obj->hasProperty (variantNames[vi]) && ! readLanes (v[variantNames[vi]], variantNames[vi], stepsPerBar, true, p.variants[(size_t) vi]))
            return std::nullopt;
    return p;
}

juce::var DrumPattern::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", juce::String (name));
    o->setProperty ("style", juce::String (style));
    if (! description.empty())
        o->setProperty ("description", juce::String (description));
    o->setProperty ("bars", bars);
    if (swing > 0.0)
        o->setProperty ("swing", swing);
    auto* lanesObj = new juce::DynamicObject();
    for (int e = 0; e < numDrumElements; ++e)
        if (! lanes[(size_t) e].empty())
            lanesObj->setProperty (elementNames[e], juce::String (lanes[(size_t) e]));
    o->setProperty ("lanes", juce::var (lanesObj));
    for (int vi = 0; vi < numDrumVariants; ++vi)
    {
        auto* vo = new juce::DynamicObject();
        bool any = false;
        for (int e = 0; e < numDrumElements; ++e)
            if (const auto& l = variants[(size_t) vi][(size_t) e])
            {
                vo->setProperty (elementNames[e], juce::String (*l));
                any = true;
            }
        if (any)
            o->setProperty (variantNames[vi], juce::var (vo));
        else
            delete vo;
    }
    return juce::var (o);
}

extern const char* const embeddedDrumPatterns[];
extern const int numEmbeddedDrumPatterns;

const std::vector<DrumPattern>& builtinDrumPatterns()
{
    static const std::vector<DrumPattern> patterns = [] {
        std::vector<DrumPattern> out;
        for (int i = 0; i < numEmbeddedDrumPatterns; ++i)
        {
            juce::String err;
            auto p = DrumPattern::fromJson (juce::JSON::parse (juce::String::fromUTF8 (embeddedDrumPatterns[i])), err);
            jassert (p.has_value()); // validated by tools/validate_patterns.py and the unit tests
            if (p)
                out.push_back (std::move (*p));
        }
        return out;
    }();
    return patterns;
}

const DrumPattern* findDrumPattern (const std::string& name)
{
    for (const auto& p : builtinDrumPatterns())
        if (p.name == name)
            return &p;
    return nullptr;
}

std::vector<const DrumPattern*> drumPatternsFor (Style style)
{
    std::vector<const DrumPattern*> out;
    for (const auto& p : builtinDrumPatterns())
        if (p.style == styleName (style))
            out.push_back (&p);
    if (out.empty() && ! builtinDrumPatterns().empty())
        out.push_back (&builtinDrumPatterns().front());
    return out;
}

std::vector<DrumNote> generateDrums (const std::vector<std::pair<double, double>>& bars, const DrumPattern& p, const DrumSettings& s)
{
    std::vector<DrumNote> notes;
    if (bars.empty())
        return notes;
    const int intensity = std::clamp (s.intensity, 1, 5);
    const double swing = std::clamp (s.swingOverride.value_or (p.swing), 0.0, 0.75);
    const double velScale = 0.8 + 0.05 * intensity; // 0.85 .. 1.05
    const int phrase = std::max (1, s.phraseBars);
    const int n = (int) bars.size();

    for (int b = 0; b < n; ++b)
    {
        const auto [barStart, barEnd] = bars[(size_t) b];
        const double stepLen = (barEnd - barStart) / DrumPattern::stepsPerBar;
        const int barInLoop = b % p.bars;
        const bool lastOfPhrase = s.transitions && (b % phrase == phrase - 1);
        const auto& overlay = p.variants[(size_t) s.transition];

        for (int e = 0; e < numDrumElements; ++e)
        {
            const auto el = (DrumElement) e;
            if ((el == DrumElement::kick && ! s.includeKick) || intensity < minIntensity (el))
                continue;
            std::string bar;
            if (lastOfPhrase && overlay[(size_t) e])
                bar = *overlay[(size_t) e]; // may be "" = silent
            else if (! p.lanes[(size_t) e].empty())
                bar = p.lanes[(size_t) e].substr ((size_t) barInLoop * DrumPattern::stepsPerBar, DrumPattern::stepsPerBar);

            for (int st = 0; st < (int) bar.size(); ++st)
            {
                const char c = bar[(size_t) st];
                if (c == '.' || (c == 'g' && intensity < 3))
                    continue;
                int vel = velocityOf (c);
                if (c == 'X' && intensity >= 5)
                    vel = 127;
                vel = std::clamp ((int) std::lround (vel * velScale), 1, 127);
                const double start = barStart + st * stepLen + (st % 2 == 1 ? swing * stepLen : 0.0);
                const double len = (el == DrumElement::openhat || el == DrumElement::ride ? 2.0 : 0.9) * stepLen;
                notes.push_back ({ start, start + len, el, vel, b, st });
            }
        }
        // riser-hit: the next downbeat lands with an accented kick + open hat
        if (lastOfPhrase && s.transition == DrumVariant::riserHit && b + 1 < n)
        {
            const double t = bars[(size_t) b + 1].first, nextStep = (bars[(size_t) b + 1].second - t) / DrumPattern::stepsPerBar;
            if (s.includeKick)
                notes.push_back ({ t, t + 0.9 * nextStep, DrumElement::kick, 127, b + 1, 0 });
            if (intensity >= minIntensity (DrumElement::openhat))
                notes.push_back ({ t, t + 4.0 * nextStep, DrumElement::openhat, 120, b + 1, 0 });
        }
    }

    // a hit the variant adds on top of the next bar's own downbeat: keep one per element and time
    std::stable_sort (notes.begin(), notes.end(), [] (const DrumNote& a, const DrumNote& b) {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.element < b.element;
    });
    notes.erase (std::unique (notes.begin(), notes.end(), [] (const DrumNote& a, const DrumNote& b) {
                     return a.element == b.element && std::abs (a.startSec - b.startSec) < 1e-9;
                 }), notes.end());
    return notes;
}

} // namespace jaudios::generators
