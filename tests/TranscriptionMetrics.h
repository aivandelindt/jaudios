#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <functional>
#include <vector>

namespace jaudios::test
{

struct RefNote
{
    double onset = 0.0, offset = 0.0;
    int midi = 0;
};

struct NoteScores
{
    int reference = 0, estimated = 0, matched = 0;
    double precision() const { return estimated ? (double) matched / estimated : 0.0; }
    double recall() const { return reference ? (double) matched / reference : 0.0; }
    double f1() const { const double p = precision(), r = recall(); return p + r > 0 ? 2 * p * r / (p + r) : 0.0; }
};

/** mir_eval.transcription.precision_recall_f1_overlap(offset_ratio=None): a reference and an
    estimated note match when their pitches are within 50 cents (same MIDI note here) and their
    onsets within `onsetTolerance` s; matches are a maximum bipartite matching. */
inline NoteScores noteF1 (const std::vector<RefNote>& ref, const std::vector<RefNote>& est, double onsetTolerance = 0.05)
{
    const size_t nr = ref.size(), ne = est.size();
    std::vector<std::vector<size_t>> adj (nr);
    for (size_t i = 0; i < nr; ++i)
        for (size_t j = 0; j < ne; ++j)
            if (ref[i].midi == est[j].midi && std::abs (ref[i].onset - est[j].onset) <= onsetTolerance + 1e-12)
                adj[i].push_back (j);

    std::vector<int> matchOfEst (ne, -1);
    std::function<bool (size_t, std::vector<char>&)> augment = [&] (size_t i, std::vector<char>& seen) {
        for (size_t j : adj[i])
        {
            if (seen[j])
                continue;
            seen[j] = 1;
            if (matchOfEst[j] < 0 || augment ((size_t) matchOfEst[j], seen))
            {
                matchOfEst[j] = (int) i;
                return true;
            }
        }
        return false;
    };
    NoteScores s { (int) nr, (int) ne, 0 };
    for (size_t i = 0; i < nr; ++i)
    {
        std::vector<char> seen (ne, 0);
        if (augment (i, seen))
            ++s.matched;
    }
    return s;
}

/** The "notes" list of a fixture's truth.json (written by tools/fixtures/FixtureRender.cpp). */
inline std::vector<RefNote> truthNotes (const juce::File& truthJson)
{
    std::vector<RefNote> notes;
    const auto v = juce::JSON::parse (truthJson);
    if (const auto* arr = v["notes"].getArray())
        for (const auto& n : *arr)
            notes.push_back ({ (double) n["onset"], (double) n["offset"], (int) n["midi"] });
    return notes;
}

} // namespace jaudios::test
