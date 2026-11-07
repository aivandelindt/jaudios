#include "HarmonySource.h"

#include <algorithm>

namespace jaudios
{

generators::HarmonicSource makeHarmonicSource (const analysis::AnalysisResult& analysis, double durationSec,
                                               const std::vector<analysis::transcription::Note>& melody,
                                               const std::vector<analysis::transcription::Note>& bass)
{
    generators::HarmonicSource src;
    if (! analysis.key.top3.empty())
        src.key = analysis.key.top3.front().key;
    src.keyMargin = analysis.key.margin;

    const auto& rhythm = analysis.rhythm;
    if (! rhythm.bars.empty())
    {
        for (const auto& bar : rhythm.bars)
        {
            std::array<float, 12> c {};
            std::copy (bar.chroma.begin(), bar.chroma.end(), c.begin());
            src.bars.push_back ({ bar.startSec, bar.endSec, c });
        }
    }
    else
    {
        // no downbeats: four beats per bar, else 2 s bars
        const auto& beats = rhythm.beats;
        if (beats.size() >= 5)
            for (size_t b = 0; b + 4 < beats.size(); b += 4)
                src.bars.push_back ({ beats[b], beats[b + 4], std::nullopt });
        else
            for (double t = 0.0; t + 1.0 < durationSec; t += 2.0)
                src.bars.push_back ({ t, std::min (t + 2.0, durationSec), std::nullopt });
    }

    for (const auto& n : melody)
        src.melody.push_back ({ n.startSec, n.endSec, n.pitch, n.confidence });
    for (const auto& n : bass)
        src.bass.push_back ({ n.startSec, n.endSec, n.pitch, n.confidence });
    return src;
}

void splitBass (const std::vector<analysis::transcription::Note>& notes, int bassBelowMidi,
                std::vector<analysis::transcription::Note>& melody, std::vector<analysis::transcription::Note>& bass)
{
    for (const auto& n : notes)
    {
        bool lowest = n.pitch < bassBelowMidi;
        if (lowest)
            for (const auto& o : notes)
                if (o.pitch < n.pitch && o.startSec < n.endSec && o.endSec > n.startSec)
                {
                    lowest = false;
                    break;
                }
        (lowest ? bass : melody).push_back (n);
    }
}

} // namespace jaudios
