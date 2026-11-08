// macOS 13+: system / per-app audio through ScreenCaptureKit (audio-capture step 2). No extra
// driver: the user grants "Screen & System Audio Recording" once (System Settings > Privacy &
// Security). JAudios' own output is excluded. Compiled with ARC.
#include "app/core/CaptureSources.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

using jaudios::CaptureRecorder;

API_AVAILABLE (macos (13.0))
@interface JAudiosStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
{
@public
    CaptureRecorder* recorder;
    float* scratch[2];      // deinterleaving, allocated before the stream starts
    int scratchFrames;
}
@end

@implementation JAudiosStreamOutput
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio || recorder == nullptr || ! CMSampleBufferIsValid (sampleBuffer))
        return;
    const auto* asbd = CMAudioFormatDescriptionGetStreamBasicDescription (CMSampleBufferGetFormatDescription (sampleBuffer));
    if (asbd == nullptr || (asbd->mFormatFlags & kAudioFormatFlagIsFloat) == 0)
        return;
    const auto frames = (int) CMSampleBufferGetNumSamples (sampleBuffer);
    struct { AudioBufferList list; AudioBuffer more[1]; } storage {};
    CMBlockBufferRef block = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (sampleBuffer, nullptr, &storage.list, sizeof (storage), nullptr, nullptr,
                                                                 kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr)
        return;
    const float* channels[2] = { nullptr, nullptr };
    int numChannels = 0;
    if ((asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0)
    {
        numChannels = juce::jmin (2, (int) storage.list.mNumberBuffers);
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch] = static_cast<const float*> (storage.list.mBuffers[ch].mData);
    }
    else if (frames <= scratchFrames)
    {
        const int interleaved = juce::jmax (1, (int) asbd->mChannelsPerFrame);
        numChannels = juce::jmin (2, interleaved);
        const auto* src = static_cast<const float*> (storage.list.mBuffers[0].mData);
        for (int i = 0; i < frames; ++i)
            for (int ch = 0; ch < numChannels; ++ch)
                scratch[ch][i] = src[i * interleaved + ch];
        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch] = scratch[ch];
    }
    if (numChannels > 0)
        recorder->push (channels, numChannels, frames);
    if (block != nullptr)
        CFRelease (block);
}
@end

namespace jaudios::platform
{
namespace
{
juce::String permissionHint (NSError* e)
{
    return juce::String ([[e localizedDescription] UTF8String] ?: "ScreenCaptureKit error")
         + " (allow JAudios under System Settings > Privacy & Security > Screen & System Audio Recording)";
}

API_AVAILABLE (macos (13.0))
SCShareableContent* shareableContent (juce::String& error)
{
    __block SCShareableContent* result = nil;
    __block NSError* failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create (0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:NO
                                                 completionHandler:^(SCShareableContent* content, NSError* e) {
                                                     result = content;
                                                     failure = e;
                                                     dispatch_semaphore_signal (done);
                                                 }];
    if (dispatch_semaphore_wait (done, dispatch_time (DISPATCH_TIME_NOW, (int64_t) 10 * NSEC_PER_SEC)) != 0)
        return (error = "ScreenCaptureKit did not answer", nil);
    if (failure != nil || result == nil)
        error = permissionHint (failure);
    return result;
}

class SystemAudioSource final : public CaptureSource
{
public:
    explicit SystemAudioSource (juce::String bundle) : bundleId (std::move (bundle)) {}
    ~SystemAudioSource() override { stop(); }

    juce::String name() const override { return bundleId.isEmpty() ? juce::String ("System audio") : bundleId; }

    bool start (CaptureRecorder& recorder, juce::String& error) override
    {
        if (@available (macOS 13.0, *))
        {
            auto* content = shareableContent (error);
            if (content == nil)
                return false;
            SCDisplay* display = content.displays.firstObject;
            if (display == nil)
                return (error = "no display to attach the capture to", false);
            SCContentFilter* filter = nil;
            if (bundleId.isEmpty())
            {
                NSMutableArray<SCRunningApplication*>* self_ = [NSMutableArray array];
                for (SCRunningApplication* app in content.applications)
                    if (app.processID == [[NSProcessInfo processInfo] processIdentifier])
                        [self_ addObject:app];
                filter = [[SCContentFilter alloc] initWithDisplay:display excludingApplications:self_ exceptingWindows:@[]];
            }
            else
            {
                NSMutableArray<SCRunningApplication*>* apps = [NSMutableArray array];
                for (SCRunningApplication* app in content.applications)
                    if ([app.bundleIdentifier isEqualToString:[NSString stringWithUTF8String:bundleId.toRawUTF8()]])
                        [apps addObject:app];
                if (apps.count == 0)
                    return (error = bundleId + " is not running", false);
                filter = [[SCContentFilter alloc] initWithDisplay:display includingApplications:apps exceptingWindows:@[]];
            }

            SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
            config.capturesAudio = YES;
            config.excludesCurrentProcessAudio = YES;
            config.sampleRate = 48000;
            config.channelCount = 2;
            config.width = 2; // audio only: the smallest video, rarely
            config.height = 2;
            config.minimumFrameInterval = CMTimeMake (1, 1);

            output = [[JAudiosStreamOutput alloc] init];
            output->recorder = &recorder;
            output->scratchFrames = 48000; // a second: more than one sample buffer
            for (auto& s : output->scratch)
                s = new float[(size_t) output->scratchFrames];
            if (! recorder.start (48000.0, 2, name()))
                return (error = "the recorder is busy", false);

            stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output];
            NSError* addError = nil;
            queue = dispatch_queue_create ("app.jaudios.capture", DISPATCH_QUEUE_SERIAL);
            [stream addStreamOutput:output type:SCStreamOutputTypeAudio sampleHandlerQueue:queue error:&addError];
            if (addError != nil)
                return (error = permissionHint (addError), false);
            __block NSError* startError = nil;
            dispatch_semaphore_t started = dispatch_semaphore_create (0);
            [stream startCaptureWithCompletionHandler:^(NSError* e) {
                startError = e;
                dispatch_semaphore_signal (started);
            }];
            dispatch_semaphore_wait (started, dispatch_time (DISPATCH_TIME_NOW, (int64_t) 10 * NSEC_PER_SEC));
            if (startError != nil)
            {
                stream = nil;
                return (error = permissionHint (startError), false);
            }
            return true;
        }
        error = "system audio capture needs macOS 13 or later";
        return false;
    }

    void stop() override
    {
        if (@available (macOS 13.0, *))
        {
            if (stream != nil)
            {
                dispatch_semaphore_t stopped = dispatch_semaphore_create (0);
                [stream stopCaptureWithCompletionHandler:^(NSError*) { dispatch_semaphore_signal (stopped); }];
                dispatch_semaphore_wait (stopped, dispatch_time (DISPATCH_TIME_NOW, (int64_t) 5 * NSEC_PER_SEC));
                stream = nil;
            }
            if (output != nil)
            {
                output->recorder = nullptr;
                for (auto& s : output->scratch)
                {
                    delete[] s;
                    s = nullptr;
                }
                output = nil;
            }
        }
    }

private:
    juce::String bundleId;
    id stream = nil;   // SCStream* (typed id: the class is macOS 13+)
    JAudiosStreamOutput* output API_AVAILABLE (macos (13.0)) = nil;
    dispatch_queue_t queue = nil;
};
} // namespace

bool systemAudioCaptureAvailable()
{
    if (@available (macOS 13.0, *))
        return true;
    return false;
}

std::vector<CaptureSourceInfo> capturableApps (juce::String& error)
{
    std::vector<CaptureSourceInfo> apps;
    if (@available (macOS 13.0, *))
    {
        auto* content = shareableContent (error);
        for (SCRunningApplication* app in content.applications)
            if (app.bundleIdentifier.length > 0 && app.processID != [[NSProcessInfo processInfo] processIdentifier])
                apps.push_back ({ "app:" + juce::String (app.bundleIdentifier.UTF8String), juce::String (app.applicationName.UTF8String), "app" });
        std::sort (apps.begin(), apps.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    }
    return apps;
}

std::unique_ptr<CaptureSource> makeSystemAudioSource (const juce::String& bundleId)
{
    return std::make_unique<SystemAudioSource> (bundleId);
}

} // namespace jaudios::platform
