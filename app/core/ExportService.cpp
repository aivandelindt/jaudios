#include "ExportService.h"

namespace jaudios
{

namespace
{
const ExportPart allParts[] = {
    { "Mix", std::nullopt },
    { "Chords", ChannelId::chords },
    { "Bass", ChannelId::bass },
    { "Drums", ChannelId::drums },
    { "Drums stem", ChannelId::stemDrums },
    { "Bass stem", ChannelId::stemBass },
    { "Other stem", ChannelId::stemOther },
    { "Vocals stem", ChannelId::stemVocals },
};
} // namespace

std::vector<ExportPart> exportableParts (bool withStems)
{
    std::vector<ExportPart> parts;
    for (const auto& p : allParts)
        if (withStems || ! p.channel || (int) *p.channel < (int) ChannelId::stemDrums)
            parts.push_back (p);
    return parts;
}

juce::String partId (const ExportPart& p)
{
    return p.name.toLowerCase().replaceCharacter (' ', '_');
}

std::optional<ExportPart> findExportPart (juce::StringRef nameOrId, bool withStems)
{
    for (const auto& p : exportableParts (withStems))
        if (partId (p) == juce::String (nameOrId).toLowerCase() || p.name.equalsIgnoreCase (nameOrId))
            return p;
    return std::nullopt;
}

juce::AudioBuffer<float> renderPart (AudioEngine& engine, const ExportPart& part, juce::int64 sourceLength, double sampleRate,
                                     double tailSeconds, int blockSize)
{
    std::array<bool, numChannelIds> solos {};
    for (int c = 0; c < numChannelIds; ++c)
    {
        solos[(size_t) c] = engine.isChannelSoloed ((ChannelId) c);
        engine.setChannelSolo ((ChannelId) c, part.channel && (int) *part.channel == c);
    }
    engine.stop();
    engine.prepareOffline (sampleRate, blockSize); // snaps the strips to the solo, resets FX and instruments
    engine.seek (0);
    engine.play();

    const int n = (int) sourceLength + (int) std::lround (std::max (0.0, tailSeconds) * sampleRate);
    juce::AudioBuffer<float> out (2, n), block (2, blockSize);
    for (int pos = 0; pos < n; pos += blockSize)
    {
        const int len = std::min (blockSize, n - pos);
        engine.renderBlock (block.getArrayOfWritePointers(), 2, len);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, len);
    }
    engine.stop();
    for (int c = 0; c < numChannelIds; ++c)
        engine.setChannelSolo ((ChannelId) c, solos[(size_t) c]);
    return out;
}

juce::String exportFileStem (const juce::String& song, const juce::String& part, const juce::String& key, double bpm)
{
    return exporting::safeFileName (song + " - " + part + " (" + key + ", " + juce::String (juce::roundToInt (bpm)) + " BPM)");
}

DragSession::DragSession()
    : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
               .getChildFile ("JAudios-drag")
               .getChildFile (juce::Uuid().toString()))
{
    dir.createDirectory();
}

DragSession::~DragSession()
{
    dir.deleteRecursively();
}

} // namespace jaudios
