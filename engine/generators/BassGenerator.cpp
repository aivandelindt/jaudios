#include "BassGenerator.h"

#include <algorithm>
#include <cmath>

namespace jaudios::generators
{

using theory::Chord;

const char* roleName (BassRole r) noexcept
{
    static const char* names[] = { "root", "third", "fifth", "octave", "chord", "scale", "approach" };
    return names[(int) r];
}

namespace
{
std::optional<BassRole> roleFromName (const juce::String& s)
{
    for (int r = 0; r <= (int) BassRole::approach; ++r)
        if (s == roleName ((BassRole) r))
            return (BassRole) r;
    return std::nullopt;
}

inline int pcMod (int x) noexcept { return ((x % 12) + 12) % 12; }
} // namespace

std::optional<BassPattern> BassPattern::fromJson (const juce::var& v, juce::String& error)
{
    // Mirrors patterns/schema.json and the semantic checks of tools/validate_patterns.py.
    auto fail = [&] (const juce::String& e) { error = e; return std::optional<BassPattern> {}; };
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return fail ("not a JSON object");
    static const juce::StringArray topKeys { "name", "style", "description", "grid", "bars", "swing", "steps" };
    for (const auto& p : obj->getProperties())
        if (! topKeys.contains (p.name.toString()))
            return fail ("unexpected property '" + p.name.toString() + "'");

    BassPattern p;
    p.name = v["name"].toString().toStdString();
    if (p.name.empty() || ! juce::String (p.name).containsOnly ("abcdefghijklmnopqrstuvwxyz0123456789_"))
        return fail ("name must be lower-case letters, digits and _");
    p.style = v["style"].toString().toStdString();
    if (! styleFromName (p.style) || p.style != styleName (*styleFromName (p.style)))
        return fail ("unknown style '" + juce::String (p.style) + "'");
    p.description = v["description"].toString().toStdString();
    if (! v["grid"].isInt() || ((int) v["grid"] != 8 && (int) v["grid"] != 16 && (int) v["grid"] != 32))
        return fail ("grid must be 8, 16 or 32");
    p.grid = v["grid"];
    if (! v["bars"].isInt() || ((int) v["bars"] != 1 && (int) v["bars"] != 2 && (int) v["bars"] != 4))
        return fail ("bars must be 1, 2 or 4");
    p.bars = v["bars"];
    if (obj->hasProperty ("swing"))
    {
        p.swing = v["swing"];
        if (! (v["swing"].isDouble() || v["swing"].isInt()) || p.swing < 0.0 || p.swing > 0.75)
            return fail ("swing must be 0 .. 0.75");
    }

    const auto* steps = v["steps"].getArray();
    if (steps == nullptr || steps->isEmpty())
        return fail ("steps must be a non-empty array");
    static const juce::StringArray stepKeys { "step", "length", "velocity", "gate", "role" };
    for (const auto& s : *steps)
    {
        const auto* so = s.getDynamicObject();
        if (so == nullptr)
            return fail ("every step must be an object");
        for (const auto& prop : so->getProperties())
            if (! stepKeys.contains (prop.name.toString()))
                return fail ("unexpected step property '" + prop.name.toString() + "'");
        BassStep st;
        if (! s["step"].isInt())
            return fail ("step must be an integer");
        st.step = s["step"];
        st.length = so->hasProperty ("length") ? (int) s["length"] : 1;
        st.velocity = so->hasProperty ("velocity") ? (int) s["velocity"] : 100;
        st.gate = so->hasProperty ("gate") ? (double) s["gate"] : 0.9;
        const auto role = roleFromName (s["role"].toString());
        if (! role)
            return fail ("unknown role '" + s["role"].toString() + "'");
        st.role = *role;
        if (st.length < 1 || st.velocity < 1 || st.velocity > 127 || st.gate < 0.05 || st.gate > 1.0)
            return fail ("step " + juce::String (st.step) + ": length >= 1, velocity 1..127, gate 0.05..1");
        p.steps.push_back (st);
    }
    std::stable_sort (p.steps.begin(), p.steps.end(), [] (const BassStep& a, const BassStep& b) { return a.step < b.step; });
    int end = -1;
    for (const auto& st : p.steps)
    {
        if (st.step < 0 || st.step >= p.totalSteps())
            return fail ("step " + juce::String (st.step) + " outside the loop (" + juce::String (p.totalSteps()) + " steps)");
        if (st.step + st.length > p.totalSteps())
            return fail ("step " + juce::String (st.step) + " runs past the loop");
        if (st.step < end)
            return fail ("step " + juce::String (st.step) + " overlaps the previous note");
        end = st.step + st.length;
    }
    return p;
}

juce::var BassPattern::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", juce::String (name));
    o->setProperty ("style", juce::String (style));
    if (! description.empty())
        o->setProperty ("description", juce::String (description));
    o->setProperty ("grid", grid);
    o->setProperty ("bars", bars);
    if (swing > 0.0)
        o->setProperty ("swing", swing);
    juce::Array<juce::var> arr;
    for (const auto& s : steps)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("step", s.step);
        so->setProperty ("length", s.length);
        so->setProperty ("velocity", s.velocity);
        so->setProperty ("gate", s.gate);
        so->setProperty ("role", roleName (s.role));
        arr.add (juce::var (so));
    }
    o->setProperty ("steps", arr);
    return juce::var (o);
}

// Generated at configure time from engine/generators/patterns/*.json (BassPatternsEmbedded.cpp).
extern const char* const embeddedBassPatterns[];
extern const int numEmbeddedBassPatterns;

const std::vector<BassPattern>& builtinBassPatterns()
{
    static const std::vector<BassPattern> patterns = [] {
        std::vector<BassPattern> out;
        for (int i = 0; i < numEmbeddedBassPatterns; ++i)
        {
            juce::String err;
            auto p = BassPattern::fromJson (juce::JSON::parse (juce::String::fromUTF8 (embeddedBassPatterns[i])), err);
            jassert (p.has_value()); // validated in CI (tools/validate_patterns.py) and by the unit tests
            if (p)
                out.push_back (std::move (*p));
        }
        return out;
    }();
    return patterns;
}

const BassPattern* findBassPattern (const std::string& name)
{
    for (const auto& p : builtinBassPatterns())
        if (p.name == name)
            return &p;
    return nullptr;
}

std::vector<const BassPattern*> bassPatternsFor (Style style)
{
    std::vector<const BassPattern*> out;
    for (const auto& p : builtinBassPatterns())
        if (p.style == styleName (style))
            out.push_back (&p);
    if (out.empty() && ! builtinBassPatterns().empty())
        out.push_back (&builtinBassPatterns().front());
    return out;
}

//==============================================================================
namespace
{
struct Placed
{
    double start, end;
    int velocity;
    BassRole role;
};

/** Nearest MIDI note with pitch class `pc` to `ref`, inside [lo, hi] when possible. */
int nearestInRange (int pc, int ref, int lo, int hi)
{
    int best = -1, bestDist = 1 << 20;
    for (int n = lo; n <= hi; ++n)
        if (pcMod (n) == pcMod (pc) && std::abs (n - ref) < bestDist)
        {
            bestDist = std::abs (n - ref);
            best = n;
        }
    if (best < 0) // range narrower than an octave: closest pitch with the pc
        best = ref + ((pcMod (pc - ref) + 6) % 12) - 6;
    return best;
}

/** The scale tone one step from `from` in direction `dir` (+1 / -1). */
int scaleStep (int from, int dir, const std::array<int, 7>& scale)
{
    for (int n = from + dir; std::abs (n - from) <= 12; n += dir)
        if (std::find (scale.begin(), scale.end(), pcMod (n)) != scale.end())
            return n;
    return from;
}

bool inScale (int pitch, const std::array<int, 7>& scale)
{
    return std::find (scale.begin(), scale.end(), pcMod (pitch)) != scale.end();
}
} // namespace

std::vector<BassNote> generateBass (const BassInput& in, const BassPattern& pattern, const BassSettings& s)
{
    std::vector<BassNote> notes;
    const auto& slots = in.progression.slots;
    const auto& chords = in.progression.chords;
    if (in.bars.empty() || slots.empty() || chords.size() != slots.size() || pattern.steps.empty())
        return notes;

    // pattern steps, optionally re-quantised to another grid (editor); colliding steps merge
    const int grid = s.gridOverride.value_or (pattern.grid);
    const double swing = std::clamp (s.swingOverride.value_or (pattern.swing), 0.0, 0.75);
    std::vector<BassStep> steps;
    for (const auto& st : pattern.steps)
    {
        BassStep x = st;
        if (grid != pattern.grid)
        {
            x.step = (int) std::floor ((double) st.step * grid / pattern.grid + 0.5);
            x.length = std::max (1, (int) std::floor ((double) st.length * grid / pattern.grid + 0.5));
        }
        if (! steps.empty() && steps.back().step == x.step)
            continue;
        steps.push_back (x);
    }
    const int loopSteps = grid * pattern.bars;

    // grid position -> seconds over the bars (steps past the last bar continue its length)
    auto timeOf = [&] (int globalStep) {
        const int bar = globalStep / grid, inBar = globalStep % grid;
        const auto n = (int) in.bars.size();
        const double barStart = bar < n ? in.bars[(size_t) bar].first : in.bars.back().second + (bar - n) * (in.bars.back().second - in.bars.back().first);
        const double barLen = bar < n ? in.bars[(size_t) bar].second - in.bars[(size_t) bar].first : in.bars.back().second - in.bars.back().first;
        const double stepLen = barLen / grid;
        return barStart + inBar * stepLen + (inBar % 2 == 1 ? swing * stepLen : 0.0);
    };

    std::vector<Placed> placed;
    const int totalBars = (int) in.bars.size();
    for (int loopStart = 0; loopStart < totalBars * grid; loopStart += loopSteps)
        for (const auto& st : steps)
        {
            const int g = loopStart + st.step;
            if (g >= totalBars * grid)
                break;
            const double start = timeOf (g);
            const double end = start + (timeOf (g + st.length) - start) * st.gate;
            placed.push_back ({ start, end, st.velocity, st.role });
        }

    auto slotAt = [&] (double t) {
        for (size_t i = 0; i < slots.size(); ++i)
            if (t >= slots[i].startSec - 1e-6 && t < slots[i].endSec - 1e-6)
                return i;
        return t < slots.front().startSec ? (size_t) 0 : slots.size() - 1;
    };
    auto isSlotStart = [&] (double t) {
        for (const auto& sl : slots)
            if (std::abs (t - sl.startSec) < 1e-6)
                return true;
        return false;
    };

    const auto scale = theory::scalePitchClasses (in.key);
    const int lo = s.lowestMidi, hi = s.highestMidi;
    int prev = nearestInRange (chords[0].root, (lo + hi) / 2 - 6, lo, hi); // start low in the register
    int rootRef = prev;

    for (size_t i = 0; i < placed.size(); ++i)
    {
        const auto& p = placed[i];
        const size_t slot = slotAt (p.start + 1e-9);
        const Chord& chord = chords[slot];
        const auto pcs = chord.pitchClasses();
        const int root = nearestInRange (chord.root, rootRef, lo, std::max (lo, hi - 7)); // room for a fifth / octave above
        BassRole role = isSlotStart (p.start) ? BassRole::root : p.role; // skill: root on the downbeat

        // the next chord (for approach / passing tones)
        const size_t nextSlot = std::min (slot + 1, slots.size() - 1);
        const bool changes = nextSlot != slot && ! (chords[nextSlot].root == chord.root);
        const int nextRoot = nearestInRange (chords[nextSlot].root, root, lo, hi);

        int pitch = root;
        switch (role)
        {
            case BassRole::root: pitch = root; break;
            case BassRole::third: pitch = nearestInRange (pcs[1], root + 3, lo, hi); break;
            case BassRole::fifth: pitch = nearestInRange (pcs.size() > 2 ? pcs[2] : pcs[1], root + 7, lo, hi); break;
            case BassRole::octave: pitch = root + 12 <= hi ? root + 12 : (root - 12 >= lo ? root - 12 : root); break;
            case BassRole::chord:
            {
                int best = root, bestDist = 1 << 20;
                for (int pc : pcs)
                {
                    const int n = nearestInRange (pc, prev, lo, hi);
                    if (n != prev && std::abs (n - prev) < bestDist)
                    {
                        bestDist = std::abs (n - prev);
                        best = n;
                    }
                }
                pitch = best;
                break;
            }
            case BassRole::scale:
            {
                const int target = changes ? nextRoot : root + 7;
                pitch = scaleStep (prev, target >= prev ? 1 : -1, scale);
                break;
            }
            case BassRole::approach:
                if (! changes)
                    pitch = nearestInRange (pcs.size() > 2 ? pcs[2] : pcs[1], root + 7, lo, hi); // no change coming: the fifth
                else
                {
                    const int below = scaleStep (nextRoot, -1, scale), above = scaleStep (nextRoot, 1, scale);
                    pitch = std::abs (below - prev) <= std::abs (above - prev) ? below : above;
                }
                break;
        }
        // a passing / approach tone must be a scale tone or a chord tone (skill acceptance)
        if (! inScale (pitch, scale) && ! chord.contains (pitch))
        {
            pitch = root;
            role = BassRole::root;
        }
        while (pitch > hi) pitch -= 12;
        while (pitch < lo) pitch += 12;

        // keep out of the way of low vocal notes (skill: < E3)
        for (const auto& v : in.vocals)
        {
            if (v.pitch >= s.vocalLowMidi || v.endSec <= p.start || v.startSec >= p.end)
                continue;
            const int d = std::abs (pitch - v.pitch);
            if (d >= 1 && d <= 4)
            {
                // same pitch class an octave down (preferred) or up, else the root in any octave
                bool moved = false;
                for (int cand : { pitch - 12, pitch + 12 })
                    if (! moved && cand >= lo && cand <= hi && std::abs (cand - v.pitch) > 4)
                    {
                        pitch = cand;
                        moved = true;
                    }
                for (int r = lo; r <= hi && ! moved; ++r)
                    if (pcMod (r) == chord.root && std::abs (r - v.pitch) > 4)
                    {
                        pitch = r;
                        role = BassRole::root;
                        moved = true;
                    }
            }
        }

        BassNote n;
        n.startSec = p.start;
        n.endSec = p.end;
        n.pitch = pitch;
        n.velocity = p.velocity;
        n.slot = (int) slot;
        n.role = role;
        notes.push_back (n);
        prev = pitch;
        rootRef = root;
    }

    // skill: octave jumps (>= 12 semitones) in at most maxOctaveJumpShare of the intervals; fold
    // the excess towards the previous note when the register allows (pitch class unchanged)
    if (notes.size() > 1)
    {
        const int allowed = (int) std::floor (s.maxOctaveJumpShare * (double) (notes.size() - 1) + 1e-9);
        int jumps = 0;
        for (size_t i = 1; i < notes.size(); ++i)
        {
            int d = notes[i].pitch - notes[i - 1].pitch;
            if (std::abs (d) < 12)
                continue;
            if (jumps < allowed)
            {
                ++jumps;
                continue;
            }
            const int folded = notes[i].pitch - 12 * (d > 0 ? 1 : -1) * (std::abs (d) / 12);
            if (folded >= lo && folded <= hi)
                notes[i].pitch = folded;
            else
                ++jumps; // cannot fold within the register; counted honestly
        }
    }

    // sidechain hint: the note starts on a kick, or holds over one
    for (auto& n : notes)
        for (double k : in.kicks)
            if (std::abs (n.startSec - k) <= s.sidechainWindowSec || (k > n.startSec && k < n.endSec))
            {
                n.sidechain = true;
                break;
            }
    return notes;
}

} // namespace jaudios::generators
