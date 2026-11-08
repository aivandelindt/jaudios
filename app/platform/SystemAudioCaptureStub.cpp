// Platforms without ScreenCaptureKit: live capture uses input devices (DeviceCaptureSource).
// Windows WASAPI loopback (audio-capture step 3) is a later step.
#include "app/core/CaptureSources.h"

namespace jaudios::platform
{
bool systemAudioCaptureAvailable() { return false; }
std::vector<CaptureSourceInfo> capturableApps (juce::String&) { return {}; }
std::unique_ptr<CaptureSource> makeSystemAudioSource (const juce::String&) { return nullptr; }
} // namespace jaudios::platform
