// chord-engine: chord model, Roman numerals, voicing, voice-leading cost.
#include "engine/theory/Chord.h"

#include <catch2/catch_test_macros.hpp>

using namespace jaudios::theory;

namespace
{
const Key C { 0, Mode::major }, Am { 9, Mode::minor }, Eb { 3, Mode::major };
}

TEST_CASE ("Chord qualities, pitch classes and names", "[theory][chord]")
{
    CHECK (Chord { 9, Quality::min }.pitchClasses() == std::vector<int> { 9, 0, 4 });
    CHECK (Chord { 7, Quality::dom7 }.pitchClasses() == std::vector<int> { 7, 11, 2, 5 });
    CHECK (Chord { 0, Quality::add9 }.pitchClasses() == std::vector<int> { 0, 4, 7, 2 });
    CHECK (Chord { 11, Quality::dim }.pitchClasses() == std::vector<int> { 11, 2, 5 });
    CHECK (Chord { 2, Quality::sus4 }.pitchClasses() == std::vector<int> { 2, 7, 9 });

    CHECK (Chord { 9, Quality::min }.name (C) == "Am");
    CHECK (Chord { 5, Quality::maj7 }.name (C) == "Fmaj7");
    CHECK (Chord { 0, Quality::maj, 1 }.name (C) == "C/E");
    CHECK (Chord { 0, Quality::maj, 1 }.bassPitchClass() == 4);
    CHECK (Chord { 10, Quality::maj }.name (Eb) == "Bb");    // flat key spelling
    CHECK (Chord { 8, Quality::maj }.name (Am) == "G#");      // A minor spells sharps (G# minor style)
    CHECK (Chord { 1, Quality::min }.name (Key { 4, Mode::major }) == "C#m");
}

TEST_CASE ("Roman numerals relative to major and natural minor", "[theory][chord][roman]")
{
    CHECK (parseRoman ("vi", Mode::major) == Degree { 9, Quality::min });
    CHECK (parseRoman ("IV", Mode::major) == Degree { 5, Quality::maj });
    CHECK (parseRoman ("bVII", Mode::major) == Degree { 10, Quality::maj }); // borrowed
    CHECK (parseRoman ("bVI", Mode::major) == Degree { 8, Quality::maj });
    CHECK (parseRoman ("iv", Mode::major) == Degree { 5, Quality::min });   // modal mixture
    CHECK (parseRoman ("vii°", Mode::major) == Degree { 11, Quality::dim });
    CHECK (parseRoman ("viio", Mode::major) == Degree { 11, Quality::dim });
    CHECK (parseRoman ("V7", Mode::major) == Degree { 7, Quality::dom7 });
    CHECK (parseRoman ("ii7", Mode::major) == Degree { 2, Quality::min7 });
    CHECK (parseRoman ("Imaj7", Mode::major) == Degree { 0, Quality::maj7 });
    CHECK (parseRoman ("Isus4", Mode::major) == Degree { 0, Quality::sus4 });
    CHECK (parseRoman ("VI", Mode::minor) == Degree { 8, Quality::maj });   // natural minor
    CHECK (parseRoman ("III", Mode::minor) == Degree { 3, Quality::maj });
    CHECK (parseRoman ("V", Mode::minor) == Degree { 7, Quality::maj });    // harmonic-minor dominant
    CHECK_FALSE (parseRoman ("VIII", Mode::major).has_value());
    CHECK_FALSE (parseRoman ("x", Mode::major).has_value());

    for (const char* r : { "I", "ii", "iii", "IV", "V", "vi", "vii°", "bVII", "bVI", "iv", "V7", "ii7", "Imaj7", "Isus2", "IVadd9", "III+" })
    {
        INFO (r);
        const auto d = parseRoman (r, Mode::major);
        REQUIRE (d);
        CHECK (romanName (*d, Mode::major) == r);
        CHECK (parseRoman (romanName (*d, Mode::major), Mode::major) == d);
    }

    CHECK (chordOf (C, *parseRoman ("vi", Mode::major)) == Chord { 9, Quality::min });
    CHECK (chordOf (Am, *parseRoman ("VII", Mode::minor)) == Chord { 7, Quality::maj });
    CHECK (romanOf (C, Chord { 10, Quality::maj }) == "bVII");
    CHECK (romanOf (Am, Chord { 5, Quality::maj }) == "VI");
}

TEST_CASE ("Diatonic check and scales", "[theory][chord]")
{
    CHECK (scalePitchClasses (Am) == std::array<int, 7> { 9, 11, 0, 2, 4, 5, 7 });
    CHECK (isDiatonic (C, Chord { 7, Quality::dom7 }));
    CHECK_FALSE (isDiatonic (C, Chord { 10, Quality::maj }));
    CHECK (isDiatonic (Am, Chord { 5, Quality::maj7 }));
    CHECK_FALSE (isDiatonic (Am, Chord { 4, Quality::maj })); // E major has G#
}

TEST_CASE ("Voicings: close, open, drop-2, inversions", "[theory][chord][voicing]")
{
    CHECK (voice (Chord { 0, Quality::maj }) == std::vector<int> { 48, 52, 55 });
    CHECK (voice (Chord { 0, Quality::maj, 1 }) == std::vector<int> { 52, 55, 60 }); // E in the bass
    CHECK (voice (Chord { 0, Quality::maj7 }, Voicing::drop2) == std::vector<int> { 48, 55, 60, 64, 71 }); // bass C + drop-2 of C E G B above it
    CHECK (voice (Chord { 0, Quality::maj }, Voicing::open) == std::vector<int> { 48, 55, 64 });   // C G E
    CHECK (voice (Chord { 9, Quality::min }, Voicing::close, 48) == std::vector<int> { 57, 60, 64 });
}

TEST_CASE ("Voice leading: cost and nearest voicing", "[theory][chord][voicing]")
{
    const Chord I { 0, Quality::maj }, IV { 5, Quality::maj }, V { 7, Quality::maj }, vi { 9, Quality::min };
    CHECK (voiceLeadingCost (I, I) == 0);
    CHECK (voiceLeadingCost (I, vi) == 2);  // C E G -> C E A
    CHECK (voiceLeadingCost (I, IV) == 3);  // C E G -> C F A
    CHECK (voiceLeadingCost (I, V) == 3);   // C E G -> B D G
    CHECK (voiceLeadingCost (I, Chord { 6, Quality::maj }) == 6); // tritone away: the most expensive triad move
    CHECK (voiceLeadingCost (I, Chord { 8, Quality::maj }) == 2);  // chromatic mediant: cheap
    CHECK (voiceLeadingCost (I, V) == voiceLeadingCost (V, I));

    const auto next = voiceLead ({ 60, 64, 67 }, V);
    CHECK (next == std::vector<int> { 59, 62, 67 });
    const auto seventh = voiceLead ({ 60, 64, 67 }, Chord { 7, Quality::dom7 });
    REQUIRE (seventh.size() == 3); // keeps three voices; drops the least essential tone (the fifth)
    for (int n : seventh)
        CHECK (Chord { 7, Quality::dom7 }.contains (n % 12));
}
