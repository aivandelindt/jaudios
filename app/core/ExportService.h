#pragma once

#include "AudioEngine.h"

#include "engine/export/Export.h"

#include <optional>

namespace jaudios
{

/** export-and-drag-drop: renders the parts the app plays, offline, through the same engine graph
    (each part soloed, with its instrument and insert FX), and writes them on the bar grid. */
struct ExportPart
{
    juce::String name;                // "Mix", "Chords", "Drums stem"
    std::optional<ChannelId> channel; // soloed; nullopt = the whole mix
};

/** The parts a song can export: the mix, the generated channels, and the stems when loaded. */
std::vector<ExportPart> exportableParts (bool withStems);
std::optional<ExportPart> findExportPart (juce::StringRef nameOrId, bool withStems); // "chords", "drums_stem", "mix"
/** "chords", "drums_stem", "mix" (CLI / project ids). */
juce::String partId (const ExportPart&);

/** Renders `part` through `engine` from the transport start: the source's `sourceLength` samples
    plus `tailSeconds` of tails (reverb, delay, releases keep ringing after the transport stops).
    The engine is re-prepared first so every render starts from the same state (strips snapped to
    the solo, FX and instruments reset). Message thread; the engine must not be on a device. */
juce::AudioBuffer<float> renderPart (AudioEngine&, const ExportPart&, juce::int64 sourceLength, double sampleRate,
                                     double tailSeconds, int blockSize = 512);

/** "Song - Chords (A minor, 124 BPM)". */
juce::String exportFileStem (const juce::String& song, const juce::String& part, const juce::String& key, double bpm);

/** A temp folder for this run's drag-and-drop files (deleted with the object). DAWs copy what is
    dropped on them, so the files only need to outlive the drag. */
class DragSession
{
public:
    DragSession();
    ~DragSession();
    const juce::File& directory() const noexcept { return dir; }

private:
    juce::File dir;
};

} // namespace jaudios
