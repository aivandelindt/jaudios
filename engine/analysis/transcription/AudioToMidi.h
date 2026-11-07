#pragma once

#include "NoteCreation.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace jaudios::analysis::transcription
{

/** One transcribed note, in seconds, with its confidence (mean note posterior, 0..1: UI shading). */
struct Note
{
    double startSec = 0.0, endSec = 0.0;
    int pitch = 0;          // MIDI
    float confidence = 0.0f;
    int velocity = 0;       // round(127 * confidence), as upstream's MIDI writer
    std::vector<int> bends; // 1/3-semitone units, one per model frame (Basic Pitch)
};

struct TranscriptionSettings
{
    NoteParams notes;          // thresholds etc. ("advanced settings")
    bool monophonic = false;   // bass stems: keep only the highest-energy note at any time
    bool pitchBends = true;
    bool multiplePitchBends = false; // upstream default: bends dropped from overlapping notes
};

struct Transcription
{
    std::vector<Note> notes;            // sorted by start, then pitch
    juce::MidiMessageSequence midi;     // channel 1; timestamps in seconds; note on/off + pitch wheel
    Posteriorgram posteriors;           // model output (diagnostics / UI)
    juce::String error;
    double seconds = 0.0;
    bool ok() const noexcept { return error.isEmpty(); }
};

/** Builds the MIDI sequence like basic_pitch.note_creation.note_events_to_midi: velocity
    round(127 * amplitude), pitch bends evenly spaced over each note, ticks round(bend * 4096 / 3)
    clamped to +-2 semitones; without multiplePitchBends, notes that overlap another note get none.
    Bends go to MIDI channel 1's pitch wheel (one voice), so overlapping notes cannot carry them. */
juce::MidiMessageSequence toMidiSequence (const std::vector<Note>&, bool multiplePitchBends = false);

/** Monophonic reduction for bass: wherever notes overlap, keep the one with the higher energy
    (confidence x duration), trimming or dropping the others. */
std::vector<Note> makeMonophonic (std::vector<Note>);

/** Audio-to-MIDI with Basic Pitch (Spotify, Apache-2.0) through ONNX Runtime (audio-to-midi skill).
    Same model input as basic_pitch.inference.run_inference: mono (channel mean) at 22.05 kHz,
    2 s windows with 30 overlapping frames, unwrapped and trimmed like upstream; then extractNotes().
    Worker thread only. */
class AudioToMidi
{
public:
    explicit AudioToMidi (juce::File modelFile);
    ~AudioToMidi();

    bool isReady() const noexcept;
    const juce::String& loadError() const noexcept { return error; }

    /** Runs the model on mono 22.05 kHz audio. */
    Posteriorgram infer (const float* mono22k, int numSamples);

    Transcription transcribe (const juce::AudioBuffer<float>& audio, double sampleRate, const TranscriptionSettings& = {});

    static juce::String expectedModelSha256();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    juce::String error;
};

} // namespace jaudios::analysis::transcription
