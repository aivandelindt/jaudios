#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace jaudios::util
{

/** SHA-256 (FIPS 180-4). Used to verify model files against models/manifest.json; ~10x faster
    than juce::SHA256 on large files. */
class Sha256
{
public:
    Sha256() noexcept;
    void update (const void* data, size_t size) noexcept;
    std::string hexDigest(); // finalises

    static std::string ofFile (const std::string& path); // empty string if unreadable

private:
    void block (const uint8_t* p) noexcept;

    uint32_t h[8];
    uint8_t buffer[64];
    size_t bufferLen = 0;
    uint64_t totalLen = 0;
};

} // namespace jaudios::util
