#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace jaudios::theory
{

enum class Mode : unsigned char { major, minor };

/** A tonal key: tonic pitch class (0 = C .. 11 = B) + mode. Pure value type, no JUCE. */
struct Key
{
    int tonic = 0;
    Mode mode = Mode::major;

    friend bool operator== (const Key&, const Key&) = default;
};

/** Our own canonical index: 0..11 = C..B major, 12..23 = C..B minor.
    NOT the OpenKeyScan class index (that mapping is recorded in openkeyscan/PREPROCESSING.md). */
int index24 (Key) noexcept;
Key keyFromIndex24 (int index) noexcept;

Key relative (Key) noexcept;

int camelotNumber (Key) noexcept;  // 1..12
char camelotLetter (Key) noexcept; // 'A' minor, 'B' major
std::string camelot (Key);         // "8A"

int openKeyNumber (Key) noexcept;  // 1..12
char openKeyLetter (Key) noexcept; // 'm' minor, 'd' major
std::string openKey (Key);         // "1m"

/** Conventional spelling: majors C Db D Eb E F F# G Ab A Bb B; minors C C# D Eb E F F# G G# A Bb B. */
std::string pitchClassName (int pitchClass, Mode);
std::string traditionalName (Key); // "A minor"

std::optional<Key> keyFromCamelot (std::string_view);
std::optional<Key> keyFromOpenKey (std::string_view);
/** Accepts "A minor", "Am", "A min", "F#m", "Gb major", "Bb" (major), case-insensitive. */
std::optional<Key> keyFromName (std::string_view);

} // namespace jaudios::theory
