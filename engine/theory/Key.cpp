#include "Key.h"

#include <algorithm>
#include <cctype>

namespace jaudios::theory
{

namespace
{
int mod12 (int x) noexcept { return ((x % 12) + 12) % 12; }

// Camelot: major C = 8B and each fifth up adds 1  ->  n = 7*pc + 8 (mod 12)
//          minor A = 8A                           ->  n = 7*pc + 5 (mod 12)
int wheelNumber (int value) noexcept
{
    const int n = mod12 (value);
    return n == 0 ? 12 : n;
}

std::string trim (std::string_view s)
{
    auto begin = s.find_first_not_of (" \t");
    auto end = s.find_last_not_of (" \t");
    return begin == std::string_view::npos ? std::string() : std::string (s.substr (begin, end - begin + 1));
}

std::optional<std::pair<int, char>> parseWheel (std::string_view text)
{
    const auto s = trim (text);
    if (s.size() < 2 || s.size() > 3)
        return std::nullopt;

    const char letter = s.back();
    const auto digits = s.substr (0, s.size() - 1);
    if (! std::all_of (digits.begin(), digits.end(), [] (unsigned char c) { return std::isdigit (c); }))
        return std::nullopt;

    const int number = std::stoi (digits);
    if (number < 1 || number > 12)
        return std::nullopt;

    return std::make_pair (number, letter);
}

/** pitch class for which `wheelNumber (7 * pc + offset) == number`. */
int pitchClassForWheel (int number, int offset) noexcept
{
    for (int pc = 0; pc < 12; ++pc)
        if (wheelNumber (7 * pc + offset) == number)
            return pc;
    return 0;
}
} // namespace

int index24 (Key k) noexcept { return mod12 (k.tonic) + (k.mode == Mode::minor ? 12 : 0); }

Key keyFromIndex24 (int index) noexcept
{
    const int i = ((index % 24) + 24) % 24;
    return { i % 12, i < 12 ? Mode::major : Mode::minor };
}

Key relative (Key k) noexcept
{
    return k.mode == Mode::major ? Key { mod12 (k.tonic + 9), Mode::minor } : Key { mod12 (k.tonic + 3), Mode::major };
}

int camelotNumber (Key k) noexcept { return wheelNumber (7 * mod12 (k.tonic) + (k.mode == Mode::major ? 8 : 5)); }
char camelotLetter (Key k) noexcept { return k.mode == Mode::major ? 'B' : 'A'; }
std::string camelot (Key k) { return std::to_string (camelotNumber (k)) + camelotLetter (k); }

// Open Key: 1d = C major = 8B, 1m = A minor = 8A  ->  same wheel shifted by 7.
int openKeyNumber (Key k) noexcept { return wheelNumber (camelotNumber (k) - 7); }
char openKeyLetter (Key k) noexcept { return k.mode == Mode::major ? 'd' : 'm'; }
std::string openKey (Key k) { return std::to_string (openKeyNumber (k)) + openKeyLetter (k); }

std::string pitchClassName (int pc, Mode mode)
{
    static const char* majorNames[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* minorNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
    return (mode == Mode::major ? majorNames : minorNames)[mod12 (pc)];
}

std::string traditionalName (Key k)
{
    return pitchClassName (k.tonic, k.mode) + (k.mode == Mode::major ? " major" : " minor");
}

std::optional<Key> keyFromCamelot (std::string_view text)
{
    const auto parsed = parseWheel (text);
    if (! parsed)
        return std::nullopt;

    const char letter = (char) std::toupper ((unsigned char) parsed->second);
    if (letter == 'B') return Key { pitchClassForWheel (parsed->first, 8), Mode::major };
    if (letter == 'A') return Key { pitchClassForWheel (parsed->first, 5), Mode::minor };
    return std::nullopt;
}

std::optional<Key> keyFromOpenKey (std::string_view text)
{
    const auto parsed = parseWheel (text);
    if (! parsed)
        return std::nullopt;

    const int camelotNum = wheelNumber (parsed->first + 7);
    const char letter = (char) std::tolower ((unsigned char) parsed->second);
    if (letter == 'd') return keyFromCamelot (std::to_string (camelotNum) + "B");
    if (letter == 'm') return keyFromCamelot (std::to_string (camelotNum) + "A");
    return std::nullopt;
}

std::optional<Key> keyFromName (std::string_view text)
{
    auto s = trim (text);
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    if (s.empty())
        return std::nullopt;

    static const int letterPc[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    if (s[0] < 'a' || s[0] > 'g')
        return std::nullopt;

    int pc = letterPc[s[0] - 'a'];
    size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == 'b'))
    {
        // "b" right after the letter is a flat unless the rest says otherwise ("bb" = B flat).
        pc += s[i] == '#' ? 1 : -1;
        ++i;
    }

    auto rest = trim (std::string_view (s).substr (i));
    Mode mode = Mode::major;
    if (rest.empty() || rest == "major" || rest == "maj" || rest == "M")
        mode = Mode::major;
    else if (rest == "m" || rest == "minor" || rest == "min")
        mode = Mode::minor;
    else
        return std::nullopt;

    return Key { mod12 (pc), mode };
}

} // namespace jaudios::theory
