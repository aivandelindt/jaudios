#pragma once

#include <juce_core/juce_core.h>

namespace jaudios
{

/** `JAudios --self-test [audio file]` (packaging-and-release smoke test): what a freshly installed
    app finds and does, without a window. Reports where every model was found (the bundle's
    Contents/Resources/models in a release), the key backend, the plugin scanner and, given a file,
    the whole analysis (key, tempo, progression). `ok` = the key model loads (ONNX), Basic Pitch and
    the scanner are there, and the file (if any) analyses. */
juce::var runSelfTest (const juce::File& audioFile, bool& ok);

} // namespace jaudios
