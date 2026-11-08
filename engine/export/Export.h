#pragma once

#include "engine/theory/Key.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <string>
#include <vector>

namespace jaudios::exporting
{

/** The musical timeline exported files share (export-and-drag-drop): beat 0 is a bar line, the
    first one at or before the start of the source (bars are prepended before the first downbeat
    when the music starts on a pickup), and beats follow the detected beat grid, so tempo drift is
    kept. A file starts at beat 0: dropped on a bar line in a DAW, its downbeats land on bar lines. */
class BarGrid
{
public:
    /** `beats` / `downbeats` in seconds from the beat tracker (downbeats a subset of beats), `bpm`
        for extrapolating outside the detected beats. No beats: a constant grid at `bpm` from 0. */
    static BarGrid fromAnalysis (const std::vector<double>& beats, const std::vector<double>& downbeats,
                                 double bpm, int beatsPerBar = 4);

    double toBeats (double seconds) const noexcept;  // fractional beat number (0 = the first bar line)
    double toSeconds (double beat) const noexcept;
    /** Where beat 0 is on the source's timeline (<= 0 when bars were prepended). */
    double originSeconds() const noexcept { return toSeconds (0.0); }
    double bpm() const noexcept { return tempo; }
    int beatsPerBar() const noexcept { return perBar; }
    /** Seconds per beat at `beat` (the tempo map). */
    double beatLength (double beat) const noexcept { return toSeconds (beat + 1.0) - toSeconds (beat); }

private:
    std::vector<double> times;   // times[i] = seconds of beat number firstBeat + i
    double firstBeat = 0.0;
    double tempo = 120.0;
    int perBar = 4;
};

/** MIDI key signature (sharps > 0, flats < 0) for a key. */
int keySignatureSharps (theory::Key) noexcept;

struct MidiPart
{
    juce::String name;                 // track name ("Chords")
    juce::MidiMessageSequence events;  // timestamps in seconds on the source timeline
};

/** A type-1 MIDI file: track 0 is the conductor (name, tempo map from the grid, 4/4, key
    signature), one track per part with its name. Ticks = beats on the grid x `ppq`. */
juce::MidiFile makeMidiFile (const juce::String& songName, const std::vector<MidiPart>& parts, const BarGrid&,
                             std::optional<theory::Key>, int ppq = 960);
bool writeMidiFile (const juce::File&, const juce::MidiFile&);

enum class WavFormat { int24, float32 };

/** Audio on the grid: `rendered` starts at the source's sample 0; the result starts at beat 0
    (silence prepended for prepended bars). Its length is everything rendered after that. */
juce::AudioBuffer<float> alignToGrid (const juce::AudioBuffer<float>& rendered, double sampleRate, const BarGrid&);

bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, WavFormat);

/** File-name safe ("Song - Chords (A minor, 124 BPM)" without / \ : * ? " < > |). */
juce::String safeFileName (const juce::String&);

} // namespace jaudios::exporting
