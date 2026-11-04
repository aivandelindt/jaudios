#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace jaudios::test
{

/** Minimal .npy reader: little-endian float32, C order, 1-D or 2-D. */
struct NpyArray
{
    std::vector<size_t> shape;
    std::vector<float> data;
};

inline std::optional<NpyArray> readNpy (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 10)
        return std::nullopt;

    const auto* bytes = static_cast<const unsigned char*> (mb.getData());
    if (std::memcmp (bytes, "\x93NUMPY", 6) != 0)
        return std::nullopt;

    const int major = bytes[6];
    const size_t headerLen = major == 1 ? (size_t) (bytes[8] | (bytes[9] << 8))
                                        : (size_t) (bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (bytes[11] << 24));
    const size_t headerStart = major == 1 ? 10 : 12;
    const juce::String header (reinterpret_cast<const char*> (bytes + headerStart), headerLen);

    if (! header.contains ("'<f4'") || header.contains ("'fortran_order': True"))
        return std::nullopt;

    NpyArray a;
    const auto shapeText = header.fromFirstOccurrenceOf ("'shape': (", false, false).upToFirstOccurrenceOf (")", false, false);
    for (const auto& token : juce::StringArray::fromTokens (shapeText, ",", ""))
        if (token.trim().isNotEmpty())
            a.shape.push_back ((size_t) token.trim().getLargeIntValue());

    size_t count = 1;
    for (auto s : a.shape)
        count *= s;

    const size_t offset = headerStart + headerLen;
    if (mb.getSize() < offset + count * sizeof (float))
        return std::nullopt;

    a.data.resize (count);
    std::memcpy (a.data.data(), bytes + offset, count * sizeof (float));
    return a;
}

} // namespace jaudios::test
