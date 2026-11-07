#pragma once

#include "ChordEngine.h"

#include <juce_core/juce_core.h>

#include <array>

namespace jaudios::generators
{

/** drum-generator skill elements. */
enum class DrumElement : unsigned char { kick, snare, clap, hat, openhat, perc, ride };
inline constexpr int numDrumElements = 7;
const char* elementName (DrumElement) noexcept;
std::optional<DrumElement> elementFromName (const juce::String&);
/** General MIDI drum note (channel 10): kick 36, snare 38, clap 39, hat 42, open hat 46, perc 37, ride 51. */
int gmNote (DrumElement) noexcept;
/** Lowest intensity (1..5) at which the element plays: kick / snare / clap 1, hat 2, open hat 3, perc 4, ride 5. */
int minIntensity (DrumElement) noexcept;

enum class DrumVariant : unsigned char { fill, riserHit, drop, halfTime };
inline constexpr int numDrumVariants = 4;
const char* variantName (DrumVariant) noexcept; // "fill", "riser_hit", "drop", "half_time"
std::optional<DrumVariant> variantFromName (const juce::String&);

/** One groove (engine/generators/drums/<name>.json, schema.json): per element a string of 1/16
    steps ('X' accent, 'x' hit, 'o' soft, 'g' ghost, '.' rest) over `bars` bars, plus one-bar
    variants that replace the last bar of a phrase (a lane missing from a variant keeps the groove;
    an empty lane is silent). */
struct DrumPattern
{
    static constexpr int stepsPerBar = 16;
    std::string name, style, description;
    int bars = 2;
    double swing = 0.0;
    std::array<std::string, numDrumElements> lanes;   // "" = element not used
    std::array<std::array<std::optional<std::string>, numDrumElements>, numDrumVariants> variants;

    static std::optional<DrumPattern> fromJson (const juce::var&, juce::String& error);
    juce::var toJson() const;
};

const std::vector<DrumPattern>& builtinDrumPatterns();
const DrumPattern* findDrumPattern (const std::string& name);
std::vector<const DrumPattern*> drumPatternsFor (Style);

struct DrumSettings
{
    bool includeKick = true;                         // off when the user has their own kick
    bool transitions = true;                         // a variant on the last bar of every phrase
    DrumVariant transition = DrumVariant::fill;
    int phraseBars = 8;
    int intensity = 3;                               // 1..5: which elements play, ghosts from 3, velocity scale
    std::optional<double> swingOverride;             // odd 16ths delayed by this fraction of a step
};

struct DrumNote
{
    double startSec = 0.0, endSec = 0.0;
    DrumElement element = DrumElement::kick;
    int velocity = 100;
    int bar = 0, step = 0;
};

/** The groove looped over `bars` (downbeat to downbeat, seconds), step positions subdividing each
    bar so the grid follows tempo drift; step 0 is the bar's downbeat itself, so kicks on the one
    land exactly on the detected downbeats (skill acceptance: within 5 ms). After a riser-hit bar
    the next downbeat gets an accented kick + open hat. Sorted by time. Deterministic. */
std::vector<DrumNote> generateDrums (const std::vector<std::pair<double, double>>& bars, const DrumPattern&, const DrumSettings& = {});

} // namespace jaudios::generators
