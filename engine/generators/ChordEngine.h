#pragma once

#include "engine/theory/Chord.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jaudios::generators
{

/** chord-engine skill styles. */
enum class Style : unsigned char { pop, house, techno, trance, lofi, rnb, cinematic };
inline constexpr int numStyles = 7;
const char* styleName (Style) noexcept;
std::optional<Style> styleFromName (const std::string&);

/** A weighted degree sequence, in Roman numerals relative to the key (theory::parseRoman). */
struct Template
{
    std::string name;                 // e.g. "vi-IV-I-V"
    std::vector<std::string> romans;
    double weight = 1.0;              // style prior, 0..1
};
const std::vector<Template>& templates (Style, theory::Mode);

//==============================================================================
/** Evidence from the source, in seconds (no JUCE or analysis types: the CLI / app fills it). */
struct SourceNote
{
    double startSec = 0.0, endSec = 0.0;
    int pitch = 0;          // MIDI
    double weight = 1.0;    // e.g. transcription confidence
};

struct BarEvidence
{
    double startSec = 0.0, endSec = 0.0;      // bar between two downbeats
    std::optional<std::array<float, 12>> chroma; // harmonic pitch-class profile of the bar
};

struct HarmonicSource
{
    theory::Key key;
    double keyMargin = 1.0;            // key-detection softmax margin (top1 - top2); low = unsure
    std::vector<BarEvidence> bars;
    std::vector<SourceNote> melody;    // e.g. the guide stems' notes (other + vocals)
    std::vector<SourceNote> bass;      // e.g. the bass stem's notes
};

enum class HarmonicRhythm : unsigned char { onePerBar, twoPerBar, onePerTwoBars };

/** score = w1 melody + w2 bass + w3 chroma + w4 prior - w5 voiceLeading (skill), per slot. */
struct Weights
{
    double melody = 1.0, bass = 0.7, chroma = 0.8, prior = 0.5, voiceLeading = 0.06;
};

struct EngineSettings
{
    Style style = Style::pop;
    HarmonicRhythm rhythm = HarmonicRhythm::onePerBar;
    int topN = 8;
    uint32_t seed = 0;            // ties are broken by a seeded hash: deterministic given the seed
    bool sevenths = false;        // keep 7th / maj7 / add9 chords from the templates (else triads)
    Weights weights;
};

struct Slot
{
    double startSec = 0.0, endSec = 0.0;
    int firstBar = 0, numBars = 1;
};

/** Slots aligned to the bars (downbeats): one per bar, two per bar, or one per two bars. */
std::vector<Slot> makeSlots (const std::vector<BarEvidence>&, HarmonicRhythm);

struct ScoreParts
{
    double melody = 0.0, bass = 0.0, chroma = 0.0, prior = 0.0, voiceLeading = 0.0; // means per slot
    double total = 0.0;
};

struct Progression
{
    std::vector<Slot> slots;
    std::vector<theory::Chord> chords;   // one per slot
    std::string source;                  // template name, or "fit" (beam search on the evidence)
    double priorWeight = 0.0;            // template weight (0 for "fit")
    ScoreParts score;
    int maxVoiceLeading = 0;             // largest semitone movement between consecutive chords

    /** Roman numerals of the chords in the source key. */
    std::vector<std::string> romans (theory::Key) const;
    /** The repeating cycle (template length or <= 4 chords), e.g. "I V vi IV". */
    std::string summary (theory::Key) const;
};

/** Top-N progressions for the source, best first (skill: N = 8, deterministic given the seed). */
std::vector<Progression> suggest (const HarmonicSource&, const EngineSettings& = {});

/** Re-scores a progression (e.g. after an edit). */
ScoreParts scoreProgression (const HarmonicSource&, const Progression&, const EngineSettings& = {});

struct Alternative
{
    theory::Chord chord;
    std::string roman;
    double score = 0.0;   // total score of the whole progression with this chord in the slot
};

/** Replacement candidates for one slot, best first (skill: 6), excluding the current chord. */
std::vector<Alternative> alternatives (const HarmonicSource&, const Progression&, int slot,
                                       const EngineSettings& = {}, int count = 6);

/** Chords the engine may use in `key` for `style`: diatonic triads + the style's borrowed chords
    (+ sevenths when enabled). */
std::vector<theory::Chord> vocabulary (theory::Key, Style, bool sevenths);

//==============================================================================
struct RenderSettings
{
    theory::Voicing voicing = theory::Voicing::close;
    int bassOctaveMidi = 48;       // C3: octave of the chord's bass note
    bool voiceLead = true;         // upper voices move to the nearest chord tones
    bool bassNote = true;          // include the chord's bass note
    int velocity = 96;
    int humanize = 0;              // +- velocity spread (skill: 8); seeded
    uint32_t seed = 0;
};

struct RenderedNote
{
    double startSec = 0.0, endSec = 0.0;
    int pitch = 0, velocity = 0, slot = 0;
};

/** Notes of the progression (one block chord per slot). */
std::vector<RenderedNote> render (const Progression&, const RenderSettings& = {});

} // namespace jaudios::generators
