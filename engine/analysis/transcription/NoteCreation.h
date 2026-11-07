#pragma once

#include <optional>
#include <vector>

namespace jaudios::analysis::transcription
{

/** Basic Pitch (basic-pitch 0.4.0, ICASSP 2022 model) geometry, from basic_pitch/constants.py. */
struct BasicPitchGeometry
{
    static constexpr int sampleRate = 22050;
    static constexpr int fftHop = 256;
    static constexpr int framesPerSecond = sampleRate / fftHop;           // 86 (integer, as upstream)
    static constexpr int windowFrames = framesPerSecond * 2;              // 172 model frames per window
    static constexpr int windowSamples = sampleRate * 2 - fftHop;         // 43844
    static constexpr int overlapFrames = 30;
    static constexpr int overlapSamples = overlapFrames * fftHop;         // 7680
    static constexpr int hopSamples = windowSamples - overlapSamples;     // 36164
    static constexpr int noteBins = 88;                                   // MIDI 21 (A0) .. 108
    static constexpr int contourBins = 88 * 3;                            // 1/3 semitone
    static constexpr int midiOffset = 21;
};

/** Unwrapped model output: frame-major (frames x bins). */
struct Posteriorgram
{
    int frames = 0;
    std::vector<float> note, onset, contour; // frames*88, frames*88, frames*264

    float noteAt (int t, int f) const noexcept { return note[(size_t) t * BasicPitchGeometry::noteBins + (size_t) f]; }
    float onsetAt (int t, int f) const noexcept { return onset[(size_t) t * BasicPitchGeometry::noteBins + (size_t) f]; }
};

/** Note-creation settings (audio-to-midi skill: frame 0.5, onset 0.5, min 58 ms; "advanced settings"). */
struct NoteParams
{
    double onsetThreshold = 0.5;   // double: upstream compares float64 arrays with Python floats
    double frameThreshold = 0.5;
    double minNoteMs = 58.0;
    bool inferOnsets = true;
    bool melodiaTrick = true;
    std::optional<double> minFrequencyHz, maxFrequencyHz;
    int energyTolerance = 11;

    /** predict()'s conversion: round(ms / 1000 * 22050 / 256). */
    int minNoteFrames() const noexcept;
};

/** A note in model frames (end exclusive), with Basic Pitch's per-frame pitch bends in 1/3 semitones. */
struct FrameNote
{
    int start = 0, end = 0, pitch = 0;
    double amplitude = 0.0;    // mean note posterior over the note: the per-note confidence
    std::vector<int> bends;    // one per frame
};

/** Port of basic_pitch.note_creation.output_to_notes_polyphonic + get_pitch_bends (0.4.0), same
    order and same decisions (onset peak picking, inferred onsets, energy tracking, melodia trick).
    Pure function, no allocation beyond its result and scratch. */
std::vector<FrameNote> extractNotes (const Posteriorgram&, const NoteParams& = {});

/** basic_pitch.note_creation.model_frames_to_time for one frame (includes upstream's per-window
    offset correction). */
double frameToSeconds (int frame) noexcept;

} // namespace jaudios::analysis::transcription
