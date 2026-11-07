#include "KeyDetector.h"

#include "KeyDetectorChroma.h"

#include "engine/analysis/openkeyscan/KeyDetectorOnnx.h"
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
    if (auto it = env.find ("JAUDIOS_ONNX_COREML"); it != env.end())
        c.onnxCoreML = it->second.trim() == "1";
    if (auto it = env.find ("JAUDIOS_MODELS_DIR"); it != env.end() && it->second.isNotEmpty())
        c.modelsDir = juce::File (it->second.trim());
    if (auto it = env.find ("OPENKEYSCAN_SIDECAR_ARGS"); it != env.end())
        c.sidecarArguments = juce::StringArray::fromTokens (it->second, " ", "\"");
    c.sidecarArguments.removeEmptyStrings();
    return c;
}

AppConfig AppConfig::fromEnvironment()
{
    std::map<juce::String, juce::String> env;
    for (const char* name : { "KEY_BACKEND", "OPENKEYSCAN_SIDECAR", "OPENKEYSCAN_SIDECAR_ARGS", "JAUDIOS_MODELS_DIR", "JAUDIOS_ONNX_COREML" })
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

juce::Array<juce::File> modelSearchLocations()
{
    juce::Array<juce::File> dirs;
   #if JUCE_MAC
    dirs.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/JAudios/models"));
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    dirs.add (exe.getParentDirectory().getSiblingFile ("Resources").getChildFile ("models"));
   #else
    dirs.add (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("JAudios/models"));
    dirs.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("resources/models"));
   #endif
   #ifdef JAUDIOS_DEV_MODELS_DIR
    dirs.add (juce::File (JAUDIOS_DEV_MODELS_DIR));
   #endif
    return dirs;
}

juce::File findModelFile (const AppConfig& config, const juce::String& fileName)
{
    if (config.modelsDir != juce::File())
        return config.modelsDir.getChildFile (fileName);
    for (const auto& dir : modelSearchLocations())
        if (dir.getChildFile (fileName).existsAsFile())
            return dir.getChildFile (fileName);
    return {};
}

static juce::File findModel (const AppConfig& config) { return findModelFile (config, "openkeyscan3.onnx"); }

KeyDetectorHandle makeKeyDetector (const AppConfig& config)
{
    KeyDetectorHandle h;
    h.detector = std::make_unique<KeyDetectorChroma>();
    h.backend = KeyBackend::chroma;

    auto requested = config.keyBackend.trim().toLowerCase();
    if (requested.isEmpty())
        requested = "onnx"; // default
    if (requested == "chroma")
        return h;

    if (requested == "onnx" || requested == "auto")
    {
        const auto model = findModel (config);
        if (model.existsAsFile())
        {
            openkeyscan::OnnxConfig oc;
            oc.modelFile = model;
            oc.preferCoreML = config.onnxCoreML;
            auto onnx = std::make_unique<openkeyscan::KeyDetectorOnnx> (oc);
            if (onnx->isReady())
            {
                h.detector = std::move (onnx);
                h.backend = KeyBackend::onnx;
                return h;
            }
            if (requested == "onnx")
            {
                h.warning = "Key model could not be loaded (" + onnx->loadError() + "); using chroma";
                return h;
            }
        }
        else if (requested == "onnx")
        {
            h.warning = "Key model openkeyscan3.onnx not found (run `make models`); using chroma";
            return h;
        }
    }

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

    h.warning = "Unknown KEY_BACKEND '" + config.keyBackend + "'; using chroma";

    return h;
}

} // namespace jaudios::analysis
