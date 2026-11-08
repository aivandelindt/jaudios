#pragma once

#include "CaptureRecorder.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace jaudios
{

/** Where a live capture comes from (audio-capture step 2). ids: "system" (everything the Mac plays
    but JAudios, macOS 13+), "app:<bundle id>" (one app, e.g. the DAW), "device:<input name>" (any
    input device: BlackHole, an aggregate device, an interface). */
struct CaptureSourceInfo
{
    juce::String id, name, kind; // kind: "system", "app", "device"
};

class CaptureSource
{
public:
    virtual ~CaptureSource() = default;
    /** Starts delivering audio into `recorder` (which it start()s with the real rate). */
    virtual bool start (CaptureRecorder&, juce::String& error) = 0;
    virtual void stop() = 0;
    virtual juce::String name() const = 0;
};

/** Every source available now (system and apps need macOS 13 and the Screen & System Audio
    Recording permission; without it they are listed with the reason in `error`). Message thread. */
std::vector<CaptureSourceInfo> listCaptureSources (juce::String& error);
std::unique_ptr<CaptureSource> makeCaptureSource (const juce::String& id, juce::String& error);

/** Any input device through JUCE: the fallback everywhere (step 2) and the path on every platform.
    The device callback pushes into the recorder (realtime-safe). */
class DeviceCaptureSource final : public CaptureSource, public juce::AudioIODeviceCallback
{
public:
    explicit DeviceCaptureSource (juce::String deviceName);
    ~DeviceCaptureSource() override;
    bool start (CaptureRecorder&, juce::String& error) override;
    void stop() override;
    juce::String name() const override { return deviceName; }

    // the device's callbacks (public so a test can drive them like a device would)
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceStopped() override {}
    void attach (CaptureRecorder& r) noexcept { recorder = &r; } // tests: without opening a device

private:
    juce::String deviceName;
    juce::AudioDeviceManager devices;
    CaptureRecorder* recorder = nullptr;
};

namespace platform
{
/** macOS 13+ ScreenCaptureKit system audio (app/platform/mac); false elsewhere. */
bool systemAudioCaptureAvailable();
/** Running apps that can be captured (bundle id + name); blocks until ScreenCaptureKit answers. */
std::vector<CaptureSourceInfo> capturableApps (juce::String& error);
/** bundleId empty = every app but JAudios. */
std::unique_ptr<CaptureSource> makeSystemAudioSource (const juce::String& bundleId);
} // namespace platform

} // namespace jaudios
