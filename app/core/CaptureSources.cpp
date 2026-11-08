#include "CaptureSources.h"

namespace jaudios
{

DeviceCaptureSource::DeviceCaptureSource (juce::String n) : deviceName (std::move (n)) {}

DeviceCaptureSource::~DeviceCaptureSource() { stop(); }

bool DeviceCaptureSource::start (CaptureRecorder& r, juce::String& error)
{
    recorder = &r;
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = deviceName;
    setup.outputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setRange (0, 2, true);
    setup.useDefaultOutputChannels = false;
    error = devices.initialise (2, 0, nullptr, false, deviceName, &setup);
    if (error.isEmpty() && devices.getCurrentAudioDevice() == nullptr)
        error = "could not open " + deviceName;
    if (error.isNotEmpty())
        return false;
    devices.addAudioCallback (this); // aboutToStart starts the recorder at the device's rate
    return recorder->isRecording();
}

void DeviceCaptureSource::stop()
{
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
}

void DeviceCaptureSource::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (recorder != nullptr && ! recorder->isRecording())
        recorder->start (device->getCurrentSampleRate(), juce::jmax (1, device->getActiveInputChannels().countNumberOfSetBits()),
                         device->getName());
}

void DeviceCaptureSource::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                                            int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    if (recorder != nullptr && numInputs > 0)
        recorder->push (inputs, numInputs, numSamples);
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
}

//==============================================================================
std::vector<CaptureSourceInfo> listCaptureSources (juce::String& error)
{
    std::vector<CaptureSourceInfo> sources;
    if (platform::systemAudioCaptureAvailable())
    {
        sources.push_back ({ "system", "System audio (every app but JAudios)", "system" });
        for (auto& app : platform::capturableApps (error))
            sources.push_back (std::move (app));
    }
    juce::AudioDeviceManager devices;
    for (auto* type : devices.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        for (const auto& n : type->getDeviceNames (true))
            sources.push_back ({ "device:" + n, n + " (" + type->getTypeName() + ")", "device" });
    }
    return sources;
}

std::unique_ptr<CaptureSource> makeCaptureSource (const juce::String& id, juce::String& error)
{
    if (id == "system" || id.startsWith ("app:"))
    {
        if (! platform::systemAudioCaptureAvailable())
            return (error = "system audio capture needs macOS 13 or later (use an input device such as BlackHole)", nullptr);
        return platform::makeSystemAudioSource (id.startsWith ("app:") ? id.fromFirstOccurrenceOf ("app:", false, false) : juce::String());
    }
    if (id.startsWith ("device:"))
        return std::make_unique<DeviceCaptureSource> (id.fromFirstOccurrenceOf ("device:", false, false));
    error = "unknown capture source " + id + " (system, app:<bundle id>, device:<name>)";
    return nullptr;
}

} // namespace jaudios
