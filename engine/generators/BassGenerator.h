#pragma once

#include "ChordEngine.h"

#include <juce_core/juce_core.h>

namespace jaudios::generators
{

/** Where a pattern step takes its pitch from, resolved against the chord sounding at that step. */
enum class BassRole : unsigned char { root, third, fifth, octave, chord, scale, approach };
const char* roleName (BassRole) noexcept;

struct BassStep
{
    int step = 0;          // grid position in the loop
    int length = 1;        // in grid steps
    int velocity = 100;    // 1..127 (accent editor)
    double gate = 0.9;     // fraction of the length that sounds (accent editor)
    BassRole role = BassRole::root;
};

/** One looping bass rhythm (engine/generators/patterns/<name>.json, schema.json). */
struct BassPattern
{
    std::string name, style, description;
    int grid = 16;         // steps per 4/4 bar: 8, 16 or 32
    int bars = 1;          // loop length: 1, 2 or 4
    double swing = 0.0;    // delay of every odd step, as a fraction of a step (0 .. 0.75)
    std::vector<BassStep> steps;

    int totalSteps() const noexcept { return grid * bars; }

    /** Parses and validates (the rules of schema.json + tools/validate_patterns.py); nullopt with `error`. */
    static std::optional<BassPattern> fromJson (const juce::var&, juce::String& error);
    juce::var toJson() const;
};

/** The patterns in engine/generators/patterns/, embedded at build time. */
const std::vector<BassPattern>& builtinBassPatterns();
const BassPattern* findBassPattern (const std::string& name);
/** The style's patterns (at least one for every style). */
std::vector<const BassPattern*> bassPatternsFor (Style);

//==============================================================================
struct BassInput
{
    theory::Key key;
    std::vector<std::pair<double, double>> bars; // bar start / end (downbeats), seconds
    Progression progression;                     // chord per slot (ChordEngine)
    std::vector<double> kicks;                   // kick onsets, seconds (drums stem / mix low band)
    std::vector<SourceNote> vocals;              // the vocal stem's notes (low ones are avoided)
};

struct BassSettings
{
    int lowestMidi = 28;               // E1
    int highestMidi = 52;              // E3
    double maxOctaveJumpShare = 0.2;   // skill: octave jumps <= 20 %
    int vocalLowMidi = 52;             // skill: avoid clashing with vocal notes below E3
    double sidechainWindowSec = 0.03;  // a note starting within this of a kick, or holding over one
    std::optional<int> gridOverride;   // re-quantise the pattern to 8 / 16 / 32 (editor)
    std::optional<double> swingOverride;
};

struct BassNote
{
    double startSec = 0.0, endSec = 0.0;
    int pitch = 0, velocity = 0, slot = 0;
    BassRole role = BassRole::root;
    bool sidechain = false;            // overlaps a kick: duck it (fx-chain sidechain hint)
};

/** Generates the bassline: the pattern loops over the bars; every step takes its pitch from its
    role on the chord sounding then. Rules (skill): the root on each chord's first downbeat;
    passing ("scale") and approach tones are scale tones of the key (no chromatic notes), so every
    note is a chord tone or a scale tone; at most `maxOctaveJumpShare` of the intervals are octave
    leaps or larger; notes within a third (but not an octave / unison) of a simultaneous vocal note
    below E3 move an octave down or fall back to the root. Deterministic. */
std::vector<BassNote> generateBass (const BassInput&, const BassPattern&, const BassSettings& = {});

} // namespace jaudios::generators
