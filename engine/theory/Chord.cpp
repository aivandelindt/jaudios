#include "Chord.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <functional>

namespace jaudios::theory
{

namespace
{
inline int pcMod (int x) noexcept { return ((x % 12) + 12) % 12; }

/** Circular semitone distance between two pitch classes. */
inline int pcDistance (int a, int b) noexcept
{
    const int d = pcMod (a - b);
    return std::min (d, 12 - d);
}

constexpr std::array<int, 7> majorSteps { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<int, 7> minorSteps { 0, 2, 3, 5, 7, 8, 10 };
const std::array<int, 7>& stepsOf (Mode m) noexcept { return m == Mode::major ? majorSteps : minorSteps; }

bool isLowerCaseQuality (Quality q) noexcept { return q == Quality::min || q == Quality::dim || q == Quality::min7; }

/** Fifths in the key signature of `key` (its relative major for minor keys): -6..+6. */
int signatureFifths (Key key) noexcept
{
    const int majorTonic = key.mode == Mode::major ? key.tonic : pcMod (key.tonic + 3);
    int f = pcMod (majorTonic * 7); // C=0 G=1 D=2 ... F=11
    return f > 6 ? f - 12 : f;
}

std::string spell (int pc, Key key)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    const int f = signatureFifths (key);
    if (f > 0)
        return sharps[pcMod (pc)];
    if (f < 0 || key.mode == Mode::major)
        return flats[pcMod (pc)];
    return pitchClassName (pcMod (pc), Mode::minor);
}

const char* const numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
} // namespace

const std::vector<int>& intervals (Quality q) noexcept
{
    static const std::vector<int> table[numQualities] = {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 3, 6 }, { 0, 4, 8 }, { 0, 2, 7 }, { 0, 5, 7 },
        { 0, 4, 7, 10 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 }, { 0, 4, 7, 14 },
    };
    return table[(int) q];
}

const char* suffix (Quality q) noexcept
{
    static const char* table[numQualities] = { "", "m", "dim", "aug", "sus2", "sus4", "7", "m7", "maj7", "add9" };
    return table[(int) q];
}

std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> pcs;
    for (int i : intervals (quality))
        pcs.push_back (pcMod (root + i));
    return pcs;
}

int Chord::bassPitchClass() const
{
    const auto pcs = pitchClasses();
    return pcs[(size_t) std::clamp (inversion, 0, (int) pcs.size() - 1)];
}

bool Chord::contains (int pc) const
{
    const auto pcs = pitchClasses();
    return std::find (pcs.begin(), pcs.end(), pcMod (pc)) != pcs.end();
}

std::string Chord::name (Key key) const
{
    std::string n = spell (root, key) + suffix (quality);
    if (inversion > 0)
        n += "/" + spell (bassPitchClass(), key);
    return n;
}

std::array<int, 7> scalePitchClasses (Key key)
{
    std::array<int, 7> pcs {};
    for (size_t i = 0; i < 7; ++i)
        pcs[i] = pcMod (key.tonic + stepsOf (key.mode)[i]);
    return pcs;
}

bool isDiatonic (Key key, const Chord& chord)
{
    const auto scale = scalePitchClasses (key);
    for (int pc : chord.pitchClasses())
        if (std::find (scale.begin(), scale.end(), pc) == scale.end())
            return false;
    return true;
}

std::optional<Degree> parseRoman (std::string_view r, Mode mode)
{
    size_t i = 0;
    int accidental = 0;
    if (i < r.size() && (r[i] == 'b' || r[i] == '#'))
        accidental = r[i++] == 'b' ? -1 : 1;

    // numeral: longest match, all upper or all lower case
    int step = -1;
    bool upper = false;
    for (int s = 6; s >= 0 && step < 0; --s) // VII before VI before V, etc.
    {
        const std::string_view up = numerals[s];
        if (r.size() - i < up.size())
            continue;
        bool allUpper = true, allLower = true;
        for (size_t k = 0; k < up.size(); ++k)
        {
            const char c = r[i + k];
            allUpper = allUpper && c == up[k];
            allLower = allLower && c == (char) std::tolower ((unsigned char) up[k]);
        }
        if (allUpper || allLower)
        {
            // "IV" must not be read as "I" + "V...": prefer the longest numeral (loop order does that),
            // but reject a match followed by more numeral letters
            const size_t end = i + up.size();
            if (end < r.size() && (r[end] == 'I' || r[end] == 'V' || r[end] == 'i' || r[end] == 'v'))
                continue;
            step = s;
            upper = allUpper;
            i = end;
        }
    }
    if (step < 0)
        return std::nullopt;

    const std::string_view rest = r.substr (i);
    Quality q;
    if (rest.empty())                                   q = upper ? Quality::maj : Quality::min;
    else if (rest == "°" || rest == "o")                q = Quality::dim;
    else if (rest == "+")                               q = Quality::aug;
    else if (rest == "7")                               q = upper ? Quality::dom7 : Quality::min7;
    else if (rest == "maj7" && upper)                   q = Quality::maj7;
    else if (rest == "sus2" && upper)                   q = Quality::sus2;
    else if (rest == "sus4" && upper)                   q = Quality::sus4;
    else if (rest == "add9" && upper)                   q = Quality::add9;
    else return std::nullopt;
    if ((q == Quality::dim) && upper)
        return std::nullopt;
    if (q == Quality::aug && ! upper)
        return std::nullopt;

    return Degree { pcMod (stepsOf (mode)[(size_t) step] + accidental), q };
}

std::string romanName (const Degree& d, Mode mode)
{
    const auto& steps = stepsOf (mode);
    const int s = pcMod (d.semitones);
    std::string prefix;
    int step = -1;
    for (int k = 0; k < 7; ++k)
        if (steps[(size_t) k] == s)
            step = k;
    if (step < 0) // chromatic: flat of the step above, unless that would be "bI"; then sharp
    {
        for (int k = 1; k < 7 && step < 0; ++k)
            if (steps[(size_t) k] == s + 1)
            {
                step = k;
                prefix = "b";
            }
        for (int k = 0; k < 7 && step < 0; ++k)
            if (pcMod (steps[(size_t) k] + 1) == s)
            {
                step = k;
                prefix = "#";
            }
    }
    std::string numeral = numerals[step];
    if (isLowerCaseQuality (d.quality))
        for (auto& c : numeral)
            c = (char) std::tolower ((unsigned char) c);

    static const char* suffixes[numQualities] = { "", "", "°", "+", "sus2", "sus4", "7", "7", "maj7", "add9" };
    return prefix + numeral + suffixes[(int) d.quality];
}

Chord chordOf (Key key, const Degree& d, int inversion)
{
    return { pcMod (key.tonic + d.semitones), d.quality, inversion };
}

std::string romanOf (Key key, const Chord& chord)
{
    return romanName ({ pcMod (chord.root - key.tonic), chord.quality }, key.mode);
}

std::vector<int> voice (const Chord& chord, Voicing v, int bassOctaveMidi)
{
    auto pcs = chord.pitchClasses();
    const int inv = std::clamp (chord.inversion, 0, (int) pcs.size() - 1);
    std::rotate (pcs.begin(), pcs.begin() + inv, pcs.end());

    auto stack = [&] (int bottom) {
        std::vector<int> notes { bottom };
        for (size_t k = 1; k < pcs.size(); ++k)
        {
            int n = notes.back() + 1;
            while (pcMod (n) != pcs[k])
                ++n;
            notes.push_back (n);
        }
        return notes;
    };
    const int bass = bassOctaveMidi + pcMod (pcs[0] - bassOctaveMidi);
    auto notes = stack (bass);

    if (v == Voicing::open)
    {
        for (size_t k = 1; k < notes.size(); k += 2)
            notes[k] += 12;
        std::sort (notes.begin(), notes.end());
    }
    else if (v == Voicing::drop2)
    {
        auto upper = stack (bass + 12);
        if (upper.size() >= 2)
            upper[upper.size() - 2] -= 12;
        notes = { bass };
        notes.insert (notes.end(), upper.begin(), upper.end());
        std::sort (notes.begin(), notes.end());
    }
    return notes;
}

namespace
{
/** Importance of chord tone k when there are fewer voices than tones: root, third, 7th/9th, fifth. */
int toneImportance (size_t k)
{
    if (k == 0) return 8; // root
    if (k == 1) return 4; // third (or the sus tone)
    if (k == 3) return 2; // seventh / ninth
    return 1;             // fifth
}

/** Nearest MIDI note to `from` with pitch class `pc` (ties: the lower one). */
int nearestNote (int from, int pc)
{
    const int down = from - pcMod (from - pc);
    const int up = down + 12;
    return (from - down) <= (up - from) ? down : up;
}
} // namespace

std::vector<int> voiceLead (const std::vector<int>& previous, const Chord& next)
{
    const auto pcs = next.pitchClasses();
    if (previous.empty())
        return {};
    const size_t voices = previous.size(), tones = pcs.size();

    // brute force over tone assignments (<= 4 tones ^ <= 6 voices): maximise covered importance,
    // then minimise movement
    std::vector<size_t> pick (voices, 0), best;
    int bestCover = -1, bestMove = INT_MAX;
    std::function<void (size_t)> search = [&] (size_t v) {
        if (v == voices)
        {
            int cover = 0, move = 0;
            std::vector<char> used (tones, 0);
            for (size_t i = 0; i < voices; ++i)
            {
                used[pick[i]] = 1;
                move += std::abs (nearestNote (previous[i], pcs[pick[i]]) - previous[i]);
            }
            for (size_t k = 0; k < tones; ++k)
                if (used[k])
                    cover += toneImportance (k);
            if (cover > bestCover || (cover == bestCover && move < bestMove))
            {
                bestCover = cover;
                bestMove = move;
                best = pick;
            }
            return;
        }
        for (size_t k = 0; k < tones; ++k)
        {
            pick[v] = k;
            search (v + 1);
        }
    };
    search (0);

    std::vector<int> out;
    for (size_t i = 0; i < voices; ++i)
        out.push_back (nearestNote (previous[i], pcs[best[i]]));
    std::sort (out.begin(), out.end());
    return out;
}

int voiceLeadingCost (const Chord& from, const Chord& to)
{
    auto a = from.pitchClasses(), b = to.pitchClasses();
    if (a.size() > b.size())
        std::swap (a, b);
    // match every tone of the smaller chord to a distinct tone of the larger one; the larger one's
    // unmatched tones pay their distance to the nearest tone of the smaller chord
    std::vector<size_t> perm (b.size());
    for (size_t i = 0; i < perm.size(); ++i)
        perm[i] = i;
    int best = INT_MAX;
    do
    {
        int cost = 0;
        for (size_t i = 0; i < a.size(); ++i)
            cost += pcDistance (a[i], b[perm[i]]);
        for (size_t j = a.size(); j < b.size(); ++j)
        {
            int nearest = 12;
            for (int x : a)
                nearest = std::min (nearest, pcDistance (x, b[perm[j]]));
            cost += nearest;
        }
        best = std::min (best, cost);
    } while (std::next_permutation (perm.begin(), perm.end()));
    return best;
}

} // namespace jaudios::theory
