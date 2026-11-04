#include "KeyDetector.h"

#include "KeyDetectorChroma.h"

#include "engine/analysis/openkeyscan/KeyDetectorSidecar.h"

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
    if (auto it = env.find ("OPENKEYSCAN_SIDECAR"); it != env.end() && it->second.isNotEmpty())
        c.sidecarExecutable = juce::File (it->second.trim());
    if (auto it = env.find ("OPENKEYSCAN_SIDECAR_ARGS"); it != env.end())
        c.sidecarArguments = juce::StringArray::fromTokens (it->second, " ", "\"");
    c.sidecarArguments.removeEmptyStrings();
    return c;
}

AppConfig AppConfig::fromEnvironment()
{
    std::map<juce::String, juce::String> env;
    for (const char* name : { "KEY_BACKEND", "OPENKEYSCAN_SIDECAR", "OPENKEYSCAN_SIDECAR_ARGS" })
    {
        const auto value = juce::SystemStats::getEnvironmentVariable (name, {});
        if (value.isNotEmpty())
            env[name] = value;
    }
    return fromEnvironment (env);
}

juce::Array<juce::File> sidecarSearchLocations()
{
    juce::Array<juce::File> locations;
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    const juce::String name = "openkeyscan-analyzer.exe";
   #else
    const juce::String name = "openkeyscan-analyzer";
   #endif

   #if JUCE_MAC
    // App.app/Contents/MacOS/JAudios -> App.app/Contents/Resources/sidecar/...
    locations.add (exe.getParentDirectory().getSiblingFile ("Resources").getChildFile ("sidecar/openkeyscan-analyzer").getChildFile (name));
   #endif
    locations.add (exe.getParentDirectory().getChildFile ("resources/sidecar/openkeyscan-analyzer").getChildFile (name));
   #ifdef JAUDIOS_DEV_SIDECAR_DIR
    locations.add (juce::File (JAUDIOS_DEV_SIDECAR_DIR).getChildFile (name)); // `make models` output
   #endif
    return locations;
}

KeyDetectorHandle makeKeyDetector (const AppConfig& config)
{
    KeyDetectorHandle h;
    h.detector = std::make_unique<KeyDetectorChroma>();
    h.backend = KeyBackend::chroma;

    const auto requested = config.keyBackend.trim().toLowerCase();
    if (requested == "chroma" || requested.isEmpty())
        return h;

    if (requested == "sidecar" || requested == "auto")
    {
        juce::File executable = config.sidecarExecutable;
        if (executable == juce::File())
            for (const auto& candidate : sidecarSearchLocations())
                if (candidate.existsAsFile())
                {
                    executable = candidate;
                    break;
                }

        if (executable.existsAsFile())
        {
            openkeyscan::SidecarConfig sc;
            sc.executable = executable;
            sc.arguments = config.sidecarArguments;
            sc.arguments.addArray (openkeyscan::defaultServerArguments());
            h.detector = std::make_unique<openkeyscan::KeyDetectorSidecar> (sc);
            h.backend = KeyBackend::sidecar;
            return h;
        }

        if (requested == "sidecar")
            h.warning = "Key detection sidecar not found ("
                      + (executable == juce::File() ? juce::String ("run `make models`") : executable.getFullPathName())
                      + "); using chroma";
        return h; // auto: chroma is the expected fallback, no warning
    }

    if (requested == "onnx")
        h.warning = "KEY_BACKEND=onnx (Phase B) is not implemented yet; using chroma";
    else
        h.warning = "Unknown KEY_BACKEND '" + config.keyBackend + "'; using chroma";

    return h;
}

} // namespace jaudios::analysis
