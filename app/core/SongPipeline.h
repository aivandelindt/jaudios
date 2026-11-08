#pragma once

#include "engine/analysis/Analysis.h"
#include "engine/analysis/AudioImport.h"
#include "engine/analysis/key/KeyDetector.h"
#include "engine/analysis/stems/StemSeparator.h"
#include "engine/analysis/transcription/AudioToMidi.h"
#include "engine/generators/BassGenerator.h"
#include "engine/generators/ChordEngine.h"
#include "engine/generators/DrumGenerator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <optional>

namespace jaudios
{

/** From a captured song to its generated parts: the pipeline the app runs on its analysis worker
    and app-cli runs from the command line (one implementation, so both behave the same). */
struct SongSettings
{
    generators::EngineSettings engine;  // style, harmonic rhythm, top-N
    bool useStems = false;              // melody / bass evidence from HT-Demucs stems (else the mix split at C3)
    bool useStemCache = true;
};

struct StemRun
{
    std::optional<analysis::stems::Stems> stems;
    juce::String provider, origin, cacheKey, error;
    double loadMs = 0.0, separateSec = 0.0;
    int segments = 0;
    bool cacheHit = false;
};

/** HT-Demucs stems, from the stem cache when it has them. Blocking: call on a worker thread. */
StemRun separateStems (const analysis::CaptureResult&, bool useCache, const std::function<void (double)>& progress = {});

/** Everything about a song before parts are generated. */
struct SongAnalysis
{
    analysis::AnalysisResult analysis;              // key, tempo, beats, downbeats, bars
    generators::HarmonicSource source;              // per-bar evidence for the chord engine
    std::vector<generators::Progression> progressions; // best first
    std::optional<analysis::stems::Stems> stems;
    std::vector<analysis::transcription::Note> vocals;  // the vocal stem's notes (with stems)
    std::vector<double> kicks;                      // kick onsets in the mix, seconds
    double sampleRate = 0.0, durationSec = 0.0;
    juce::String separation = "none";               // "none", "cache", "htdemucs (<provider>)"
    double engineMs = 0.0;
    int melodyNotes = 0, bassNotes = 0;
};

/** Blocking (worker thread): stems (optional), key + beat grid, transcription (Basic Pitch),
    evidence, kicks, top progressions. `keyDetector` null = one from the environment; `stems`
    already separated (with their `separation` label) skip the separation step. */
std::optional<SongAnalysis> analyseSong (const analysis::CaptureResult&, const SongSettings&, juce::String& error,
                                         analysis::IKeyDetector* keyDetector = nullptr,
                                         std::optional<analysis::stems::Stems> stems = std::nullopt, const juce::String& separation = {});

/** (Re)computes the suggestions for `song.source` (a change of style, or a source built in tests). */
void suggestProgressions (SongAnalysis&, const generators::EngineSettings&);

/** The bars (downbeat to downbeat, seconds) the bass and drums loop over. */
std::vector<std::pair<double, double>> songBars (const SongAnalysis&);

struct SongParts
{
    generators::Progression progression;
    std::vector<generators::RenderedNote> chordNotes;
    std::vector<generators::BassNote> bassNotes;
    std::vector<generators::DrumNote> drumNotes;
    juce::MidiMessageSequence chords, bass;          // seconds, channel 1
    const generators::BassPattern* bassPattern = nullptr;
    const generators::DrumPattern* drumPattern = nullptr; // null when drums are off
};

/** Chords, a bassline on them and a groove on the bars, for `progression` in `style`'s first
    patterns (or the named ones). Cheap: fine on the message thread. Deterministic. */
SongParts generateParts (const SongAnalysis&, const generators::Progression&, generators::Style, bool withDrums = true,
                         const generators::BassPattern* bassPattern = nullptr, const generators::DrumPattern* drumPattern = nullptr);

} // namespace jaudios
