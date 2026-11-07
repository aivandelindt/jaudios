#include "StemCache.h"

#include "engine/analysis/Log.h"
#include "engine/util/Sha256.h"

#include <algorithm>
#include <cstring>

namespace jaudios::analysis::stems
{

namespace
{
constexpr int kFormatVersion = 1;
constexpr int kChannels = 2;

bool isHexKey (const juce::String& key)
{
    return key.length() == 64 && key.containsOnly ("0123456789abcdef");
}
} // namespace

juce::File StemCache::defaultDirectory()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/JAudios/cache/stems");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("JAudios/cache/stems");
   #endif
}

StemCache::StemCache (juce::File directory) : dir (std::move (directory)) {}

juce::String StemCache::keyFor (const juce::AudioBuffer<float>& input, double sampleRate, const juce::String& separatorId)
{
    util::Sha256 h;
    const char tag[] = "jaudios-stem-cache-v1";
    h.update (tag, sizeof (tag));
    const auto id = separatorId.toStdString();
    const auto idLength = (uint64_t) id.size();
    h.update (&idLength, sizeof (idLength));
    h.update (id.data(), id.size());
    const int64_t header[] = { (int64_t) std::llround (sampleRate * 1000.0), input.getNumChannels(), input.getNumSamples() };
    h.update (header, sizeof (header));
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        h.update (input.getReadPointer (ch), sizeof (float) * (size_t) input.getNumSamples()); // little-endian hosts only (arm64, x86-64)
    return juce::String (h.hexDigest());
}

std::optional<Stems> StemCache::load (const juce::String& key) const
{
    if (! isHexKey (key))
        return std::nullopt;
    const auto entry = dir.getChildFile (key);
    const auto meta = juce::JSON::parse (entry.getChildFile ("meta.json"));
    const auto data = entry.getChildFile ("stems.f32");
    if (! meta.isObject() || (int) meta["version"] != kFormatVersion || meta["key"].toString() != key)
        return std::nullopt;

    const juce::int64 n = meta["num_samples"];
    const double sampleRate = meta["sample_rate"];
    if (n <= 0 || n > std::numeric_limits<int>::max() || sampleRate <= 0.0
        || data.getSize() != n * numStems * kChannels * (juce::int64) sizeof (float))
        return std::nullopt;

    juce::FileInputStream in (data);
    if (! in.openedOk())
        return std::nullopt;

    Stems stems;
    stems.sampleRate = sampleRate;
    for (auto& b : stems.buffers)
    {
        b.setSize (kChannels, (int) n);
        for (int ch = 0; ch < kChannels; ++ch)
        {
            const auto bytes = (size_t) n * sizeof (float);
            if ((size_t) in.read (b.getWritePointer (ch), (int) bytes) != bytes)
                return std::nullopt;
        }
    }
    entry.setLastAccessTime (juce::Time::getCurrentTime()); // LRU for prune()
    entry.getChildFile ("meta.json").setLastModificationTime (juce::Time::getCurrentTime());
    return stems;
}

bool StemCache::store (const juce::String& key, const Stems& stems) const
{
    if (! isHexKey (key) || stems.getNumSamples() <= 0)
        return false;
    for (const auto& b : stems.buffers)
        if (b.getNumChannels() != kChannels || b.getNumSamples() != stems.getNumSamples())
            return false;
    if (! dir.createDirectory())
        return false;

    const auto tmp = dir.getNonexistentChildFile (key + ".tmp", {}, false);
    if (! tmp.createDirectory())
        return false;

    bool ok = false;
    {
        juce::FileOutputStream out (tmp.getChildFile ("stems.f32"));
        ok = out.openedOk();
        for (const auto& b : stems.buffers)
            for (int ch = 0; ch < kChannels && ok; ++ch)
                ok = out.write (b.getReadPointer (ch), sizeof (float) * (size_t) b.getNumSamples());
        ok = ok && (out.flush(), out.getStatus().wasOk());
    }

    auto* meta = new juce::DynamicObject();
    meta->setProperty ("version", kFormatVersion);
    meta->setProperty ("key", key);
    meta->setProperty ("sample_rate", stems.sampleRate);
    meta->setProperty ("num_samples", stems.getNumSamples());
    meta->setProperty ("channels", kChannels);
    meta->setProperty ("stems", "drums,bass,other,vocals");
    ok = ok && tmp.getChildFile ("meta.json").replaceWithText (juce::JSON::toString (juce::var (meta)));

    const auto entry = dir.getChildFile (key);
    if (ok)
    {
        entry.deleteRecursively(); // a stale or corrupt entry under the same key
        ok = tmp.moveFileTo (entry);
    }
    if (! ok)
    {
        tmp.deleteRecursively();
        log::warning ("Stem cache: could not store " + entry.getFullPathName());
    }
    return ok;
}

int StemCache::prune (juce::int64 budgetBytes) const
{
    struct Entry { juce::File dir; juce::Time used; juce::int64 bytes; };
    std::vector<Entry> entries;
    juce::int64 total = 0;
    for (const auto& d : dir.findChildFiles (juce::File::findDirectories, false))
    {
        if (! isHexKey (d.getFileName()))
            continue;
        juce::int64 bytes = 0;
        for (const auto& f : d.findChildFiles (juce::File::findFiles, false))
            bytes += f.getSize();
        entries.push_back ({ d, d.getChildFile ("meta.json").getLastModificationTime(), bytes });
        total += bytes;
    }
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.used < b.used; });

    int removed = 0;
    for (const auto& e : entries)
    {
        if (total <= budgetBytes)
            break;
        if (e.dir.deleteRecursively())
        {
            total -= e.bytes;
            ++removed;
        }
    }
    return removed;
}

} // namespace jaudios::analysis::stems
