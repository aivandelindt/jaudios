#include "engine/theory/Key.h"

#include <catch2/catch_test_macros.hpp>

using namespace jaudios::theory;

TEST_CASE ("Camelot and Open Key wheel for all 24 keys", "[theory][key]")
{
    struct Row { const char* name; const char* camelot; const char* openKey; };
    // Reference wheel (Camelot: 8B = C major, 8A = A minor; Open Key: 1d = C major, 1m = A minor).
    const Row rows[] = {
        { "C major", "8B", "1d" },   { "Db major", "3B", "8d" },  { "D major", "10B", "3d" },
        { "Eb major", "5B", "10d" }, { "E major", "12B", "5d" },  { "F major", "7B", "12d" },
        { "F# major", "2B", "7d" },  { "G major", "9B", "2d" },   { "Ab major", "4B", "9d" },
        { "A major", "11B", "4d" },  { "Bb major", "6B", "11d" }, { "B major", "1B", "6d" },
        { "C minor", "5A", "10m" },  { "C# minor", "12A", "5m" }, { "D minor", "7A", "12m" },
        { "Eb minor", "2A", "7m" },  { "E minor", "9A", "2m" },   { "F minor", "4A", "9m" },
        { "F# minor", "11A", "4m" }, { "G minor", "6A", "11m" },  { "G# minor", "1A", "6m" },
        { "A minor", "8A", "1m" },   { "Bb minor", "3A", "8m" },  { "B minor", "10A", "3m" },
    };

    for (int i = 0; i < 24; ++i)
    {
        const Key key = keyFromIndex24 (i);
        const Row& row = rows[i];
        INFO ("index " << i);
        CHECK (traditionalName (key) == row.name);
        CHECK (camelot (key) == row.camelot);
        CHECK (openKey (key) == row.openKey);
        CHECK (keyFromCamelot (row.camelot) == key);
        CHECK (keyFromOpenKey (row.openKey) == key);
        CHECK (keyFromName (row.name) == key);
        CHECK (index24 (key) == i);
    }
}

TEST_CASE ("Golden keys from ml/tests/goldens.json", "[theory][key]")
{
    CHECK (camelot ({ 9, Mode::minor }) == "8A");
    CHECK (openKey ({ 9, Mode::minor }) == "1m");
    CHECK (camelot ({ 5, Mode::minor }) == "4A");
    CHECK (openKey ({ 5, Mode::minor }) == "9m");
    CHECK (camelot ({ 0, Mode::major }) == "8B");
    CHECK (camelot ({ 7, Mode::major }) == "9B");
    CHECK (openKey ({ 7, Mode::major }) == "2d");
    CHECK (camelot ({ 2, Mode::minor }) == "7A");
    CHECK (openKey ({ 2, Mode::minor }) == "12m");
}

TEST_CASE ("Key name parsing accepts common spellings", "[theory][key]")
{
    CHECK (keyFromName ("Am") == Key { 9, Mode::minor });
    CHECK (keyFromName ("A min") == Key { 9, Mode::minor });
    CHECK (keyFromName ("F#m") == Key { 6, Mode::minor });
    CHECK (keyFromName ("Gb major") == Key { 6, Mode::major });
    CHECK (keyFromName ("Bb") == Key { 10, Mode::major });
    CHECK (keyFromName ("a# minor") == Key { 10, Mode::minor });
    CHECK_FALSE (keyFromName ("H major").has_value());
    CHECK_FALSE (keyFromCamelot ("13A").has_value());
    CHECK_FALSE (keyFromCamelot ("8C").has_value());
    CHECK_FALSE (keyFromOpenKey ("0d").has_value());
}

TEST_CASE ("Relative keys share a Camelot number", "[theory][key]")
{
    for (int i = 0; i < 24; ++i)
    {
        const Key k = keyFromIndex24 (i);
        CHECK (camelotNumber (relative (k)) == camelotNumber (k));
        CHECK (relative (relative (k)) == k);
        CHECK (relative (k).mode != k.mode);
    }
}
