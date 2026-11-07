#pragma once

#include "StemSeparator.h"

namespace jaudios::analysis::stems
{

/** On-disk stem cache (stem-separation skill: "cache by SHA-256 of input in Application Support").

    Key = SHA-256 over the decoded input audio (sample rate, channel count, length and every float
    sample) plus StemSeparator::cacheId() (model sha256 + settings), so the same audio from any file
    or capture hits, and a different model or setting misses.
    Layout: <dir>/<key>/meta.json + stems.f32 (raw little-endian float32, stem-major, channel-major:
    exact, no codec). Entries are written to a temporary directory and renamed into place, so a
    crash never leaves a half-written entry under its key. A 3-minute track takes ~250 MB, so
    prune() keeps the most recently used entries under a size budget.
    Worker thread only (file I/O). */
class StemCache
{
public:
    /** ~/Library/Application Support/JAudios/cache/stems (macOS) or <app data>/JAudios/cache/stems. */
    static juce::File defaultDirectory();
    static constexpr juce::int64 defaultBudgetBytes = (juce::int64) 4 << 30; // 4 GiB

    explicit StemCache (juce::File directory = defaultDirectory());

    static juce::String keyFor (const juce::AudioBuffer<float>& input, double sampleRate, const juce::String& separatorId);

    /** Cached stems for `key`, or nullopt (missing, incomplete or corrupt entries are ignored). */
    std::optional<Stems> load (const juce::String& key) const;
    bool store (const juce::String& key, const Stems&) const;

    /** Deletes least recently used entries until the cache is under `budgetBytes`; returns how many. */
    int prune (juce::int64 budgetBytes = defaultBudgetBytes) const;

    const juce::File& directory() const noexcept { return dir; }

private:
    juce::File dir;
};

} // namespace jaudios::analysis::stems
