#pragma once

#include "AudioImport.h"
#include "key/KeyDetector.h"
#include "tempo/BeatTracker.h"

namespace jaudios::analysis
{

struct AnalysisResult
{
    KeyResult key;
    RhythmResult rhythm;
    double keyMs = 0.0, rhythmMs = 0.0;
};

/** Key + tempo/beat grid for an imported or captured buffer. Analysis thread only. */
AnalysisResult analyze (const CaptureResult& capture, IKeyDetector& keyDetector);

/** JSON shape printed by `app-cli analyze` (stable; consumed by tests and later the UI). */
juce::var toJson (const AnalysisResult&, const CaptureResult&, const juce::String& keyWarning = {});

} // namespace jaudios::analysis
