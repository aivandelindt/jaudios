#pragma once

#include "Key.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace jaudios::theory
{

/** chord-engine skill: maj / min / dim / aug / sus2 / sus4 / 7 / m7 / maj7 / add9. */
enum class Quality : unsigned char { maj, min, dim, aug, sus2, sus4, dom7, min7, maj7, add9 };
inline constexpr int numQualities = 10;

/** Intervals above the root, in semitones (root first). */
const std::vector<int>& intervals (Quality) noexcept;
/** Symbol suffix: "", "m", "dim", "aug", "sus2", "sus4", "7", "m7", "maj7", "add9". */
const char* suffix (Quality) noexcept;

/** A chord: root pitch class, quality, inversion (0 = root position; n = the n-th chord tone in
    the bass). Pure value type, no JUCE. */
struct Chord
{
    int root = 0; // 0 = C .. 11 = B
    Quality quality = Quality::maj;
    int inversion = 0;

    friend bool operator== (const Chord&, const Chord&) = default;

    /** Pitch classes, root first. */
    std::vector<int> pitchClasses() const;
    int bassPitchClass() const;
    bool contains (int pitchClass) const;
    /** "Am", "G7", "Fmaj7", "C/E" (slash = inversion), spelled for `key`'s signature: sharp keys sharp,
    flat keys and C major flat, A minor as theory::pitchClassName (C# F# G# Bb Eb). */
    std::string name (Key key = {}) const;
};

/** A chord as a scale degree of a key: semitones above the tonic + quality, e.g. vi = {9, min}. */
struct Degree
{
    int semitones = 0;
    Quality quality = Quality::maj;
    friend bool operator== (const Degree&, const Degree&) = default;
};

/** Roman numerals relative to the key's own scale (major, or natural minor), so in A minor "VI" is
    F and "bVI" would be Fb; in C major "bVII" is Bb (borrowed). Case gives the triad (I major,
    i minor); suffixes: "°"/"o" dim, "+" aug, "7" (dominant on upper case, m7 on lower case),
    "maj7", "sus2", "sus4", "add9". Examples: "vi", "bVII", "iv", "ii7", "V7", "Imaj7", "vii°". */
std::optional<Degree> parseRoman (std::string_view roman, Mode mode);
std::string romanName (const Degree&, Mode mode);

Chord chordOf (Key key, const Degree&, int inversion = 0);
/** The degree of `chord` in `key`, written as parseRoman would accept it. */
std::string romanOf (Key key, const Chord& chord);

/** Diatonic: every chord tone is in the key's scale (major / natural minor). */
bool isDiatonic (Key key, const Chord&);
/** The key's scale as pitch classes (major or natural minor). */
std::array<int, 7> scalePitchClasses (Key key);

enum class Voicing : unsigned char { close, open, drop2 };

/** MIDI notes of a voicing: the bass note (chord's bass pitch class) in the octave starting at
    `bassOctaveMidi`, then the upper voices. close = the tones stacked upwards from the bass within
    an octave; open = close with every other upper voice raised an octave; drop2 = the bass, plus
    all chord tones in close position from an octave above the bass with the second-highest dropped
    an octave (the chord's bass stays lowest). */
std::vector<int> voice (const Chord&, Voicing = Voicing::close, int bassOctaveMidi = 48);

/** Voices `next` as close to `previous` (sorted MIDI notes, without the bass) as possible: same
    number of voices, each moved to the nearest chord tone, all tones covered when possible. */
std::vector<int> voiceLead (const std::vector<int>& previous, const Chord& next);

/** Voice-leading cost: minimum total semitone movement mapping one voicing onto the other
    (pitch classes, nearest octave; voices are matched one-to-one, extra voices pay their
    nearest distance). Used for the chord-engine score and its <= 6 semitone acceptance. */
int voiceLeadingCost (const Chord& from, const Chord& to);

} // namespace jaudios::theory
