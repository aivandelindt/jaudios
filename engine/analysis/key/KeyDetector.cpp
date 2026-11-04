#include "KeyDetector.h"

#include "KeyDetectorChroma.h"

namespace jaudios::analysis
{

const char* toString (KeyBackend b) noexcept
{
    switch (b)
    {
        case KeyBackend::sidecar: return "sidecar";
        case KeyBackend::onnx:    return "onnx";
        case KeyBackend::chroma:  return "chroma";
    }
    return "unknown";
}

AppConfig AppConfig::fromEnvironment (const std::map<juce::String, juce::String>& env)
{
    AppConfig c;
    if (auto it = env.find ("KEY_BACKEND"); it != env.end() && it->second.isNotEmpty())
        c.keyBackend = it->second.trim().toLowerCase();
    return c;
}

AppConfig AppConfig::fromEnvironment()
{
    std::map<juce::String, juce::String> env;
    const auto value = juce::SystemStats::getEnvironmentVariable ("KEY_BACKEND", {});
    if (value.isNotEmpty())
        env["KEY_BACKEND"] = value;
    return fromEnvironment (env);
}

KeyDetectorHandle makeKeyDetector (const AppConfig& config)
{
    KeyDetectorHandle h;
    h.detector = std::make_unique<KeyDetectorChroma>();
    h.backend = KeyBackend::chroma;

    const auto requested = config.keyBackend.trim().toLowerCase();
    if (requested == "chroma" || requested.isEmpty())
        return h;

    if (requested == "sidecar" || requested == "onnx")
        h.warning = "KEY_BACKEND=" + requested + " is not available in this build (pending dependency approval); using chroma";
    else
        h.warning = "Unknown KEY_BACKEND '" + config.keyBackend + "'; using chroma";

    return h;
}

} // namespace jaudios::analysis
