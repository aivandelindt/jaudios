#include "AudioEngine.h"

#include "RealtimeTrap.h"

#include <algorithm>

namespace jaudios
{

using Graph = juce::AudioProcessorGraph;
using IO = Graph::AudioGraphIOProcessor;

static constexpr auto syncUpdate = Graph::UpdateKind::sync;

static void connectStereo (Graph& g, Graph::NodeID from, Graph::NodeID to, int fromChannel = 0)
{
    for (int ch = 0; ch < 2; ++ch)
        g.addConnection ({ { from, fromChannel + ch }, { to, ch } }, syncUpdate);
}

AudioEngine::AudioEngine()
{
    graph.setPlayConfigDetails (0, 2, 44100.0, defaultBlockSize);

    auto outNode = graph.addNode (std::make_unique<IO> (IO::audioOutputNode), {}, syncUpdate);

    auto masterNode = graph.addNode (std::make_unique<ChannelStripProcessor> ("Master"), {}, syncUpdate);
    master = static_cast<ChannelStripProcessor*> (masterNode->getProcessor());

    auto sourceNode = graph.addNode (std::make_unique<SourcePlayerProcessor>(), {}, syncUpdate);
    sourcePlayer = static_cast<SourcePlayerProcessor*> (sourceNode->getProcessor());

    auto sourceStrip = graph.addNode (std::make_unique<ChannelStripProcessor> ("Source"), {}, syncUpdate);
    strips[(size_t) ChannelId::source] = static_cast<ChannelStripProcessor*> (sourceStrip->getProcessor());

    connectStereo (graph, sourceNode->nodeID, sourceStrip->nodeID);
    connectStereo (graph, sourceStrip->nodeID, masterNode->nodeID);

    static const char* stemNames[] = { "Drums stem", "Bass stem", "Other stem", "Vocals stem" };
    for (int s = 0; s < SourcePlayerProcessor::numStemLayers; ++s)
    {
        auto strip = graph.addNode (std::make_unique<ChannelStripProcessor> (stemNames[s]), {}, syncUpdate);
        strips[(size_t) stemChannel (s)] = static_cast<ChannelStripProcessor*> (strip->getProcessor());
        connectStereo (graph, sourceNode->nodeID, strip->nodeID, 2 + 2 * s);
        connectStereo (graph, strip->nodeID, masterNode->nodeID);
    }

    static const char* generatedNames[] = { "Chords", "Bass", "Drums" };
    for (int g = 0; g < 3; ++g)
    {
        const auto id = (ChannelId) ((int) ChannelId::chords + g);
        auto player = graph.addNode (std::make_unique<ClipPlayerProcessor> (generatedNames[g]), {}, syncUpdate);
        clipPlayers[(size_t) g] = static_cast<ClipPlayerProcessor*> (player->getProcessor());
        auto strip = graph.addNode (std::make_unique<ChannelStripProcessor> (generatedNames[g]), {}, syncUpdate);
        strips[(size_t) id] = static_cast<ChannelStripProcessor*> (strip->getProcessor());
        connectStereo (graph, player->nodeID, strip->nodeID);
        connectStereo (graph, strip->nodeID, masterNode->nodeID);
    }

    connectStereo (graph, masterNode->nodeID, outNode->nodeID);
}

ClipPlayerProcessor* AudioEngine::clipPlayer (ChannelId id) const noexcept
{
    const int g = (int) id - (int) ChannelId::chords;
    return g >= 0 && g < 3 ? clipPlayers[(size_t) g] : nullptr;
}

void AudioEngine::play()
{
    sourcePlayer->play();
    for (auto* p : clipPlayers)
        p->play();
}

void AudioEngine::stop()
{
    sourcePlayer->stop();
    for (auto* p : clipPlayers)
        p->stop();
}

void AudioEngine::seek (juce::int64 position)
{
    sourcePlayer->seek (position);
    for (auto* p : clipPlayers)
        p->seek (position);
}

bool AudioEngine::setChannelClip (ChannelId id, std::shared_ptr<const MidiClip> clip)
{
    auto* p = clipPlayer (id);
    return p != nullptr && p->setClip (std::move (clip));
}

bool AudioEngine::setChannelInstrument (ChannelId id, std::shared_ptr<synth::Instrument> instrument)
{
    auto* p = clipPlayer (id);
    if (p == nullptr)
        return false;
    if (instrument != nullptr)
        instrument->prepare (preparedSampleRate > 0.0 ? preparedSampleRate : 44100.0,
                             preparedBlockSize > 0 ? preparedBlockSize : defaultBlockSize);
    return p->setInstrument (std::move (instrument));
}

void AudioEngine::setBypass (bool shouldBypass)
{
    bypassed = shouldBypass;
    updateEffectiveMutes();
}

bool AudioEngine::setSource (std::shared_ptr<const juce::AudioBuffer<float>> source)
{
    const juce::int64 length = source != nullptr ? source->getNumSamples() : -1;
    if (! sourcePlayer->setSource (std::move (source)))
        return false;
    for (auto* p : clipPlayers) // one transport: the generated parts end where the source ends
        p->setLength (length);
    stemsLoaded = false;
    updateEffectiveMutes();
    return true;
}

bool AudioEngine::setStems (std::shared_ptr<const juce::AudioBuffer<float>> stems)
{
    const bool loaded = stems != nullptr;
    if (! sourcePlayer->setStems (std::move (stems)))
        return false;
    stemsLoaded = loaded;
    updateEffectiveMutes();
    return true;
}

AudioEngine::~AudioEngine()
{
    closeDevice();
    graph.releaseResources();
}

juce::String AudioEngine::openDefaultDevice (int preferredBufferSize)
{
    auto error = deviceManager.initialiseWithDefaultDevices (0, 2);
    if (error.isNotEmpty())
        return error;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = preferredBufferSize;
    error = deviceManager.setAudioDeviceSetup (setup, true);
    if (error.isNotEmpty())
        return error;

    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return "No audio output device available";

    deviceManager.addAudioCallback (this);
    return {};
}

void AudioEngine::closeDevice()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::prepareOffline (double sampleRate, int blockSize)
{
    prepare (sampleRate, blockSize);
}

void AudioEngine::prepare (double sampleRate, int blockSize)
{
    prepared = false;
    graph.releaseResources();
    graph.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);

    scratch.setSize (2, blockSize, false, true, false);
    midiScratch.ensureSize (2048);
    pendingSamples = 0;

    // Warm-up: the graph's render sequence sizes some internal buffers on the first
    // processBlock call. Do that here (not on the audio thread) with every node suspended, so
    // no source/transport state advances.
    for (auto* node : graph.getNodes())
        node->getProcessor()->suspendProcessing (true);
    renderGraphBlock();
    for (auto* node : graph.getNodes())
        node->getProcessor()->suspendProcessing (false);

    preparedSampleRate = sampleRate;
    preparedBlockSize = blockSize;
    prepared = true;
}

void AudioEngine::renderBlock (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const rt::ScopedRealtimeSection realtime;
    const juce::ScopedNoDenormals noDenormals;

    if (! prepared)
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);
        return;
    }

    // The graph is always called with exactly preparedBlockSize samples (JUCE's render sequence
    // reallocates its output buffer whenever the block size changes). Device blocks of any size
    // are served from the last rendered block; this renders ahead but adds no latency.
    int delivered = 0;
    while (delivered < numSamples)
    {
        if (pendingSamples == 0)
        {
            renderGraphBlock();
            pendingSamples = preparedBlockSize;
        }

        const int start = preparedBlockSize - pendingSamples;
        const int n = std::min (pendingSamples, numSamples - delivered);

        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (ch < 2)
                juce::FloatVectorOperations::copy (outputs[ch] + delivered, scratch.getReadPointer (ch, start), n);
            else
                juce::FloatVectorOperations::clear (outputs[ch] + delivered, n);
        }

        pendingSamples -= n;
        delivered += n;
    }
}

void AudioEngine::renderGraphBlock() noexcept
{
    midiScratch.clear();
    graph.processBlock (scratch, midiScratch);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs,
                                                    int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    renderBlock (outputs, numOutputs, numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceStopped()
{
    prepared = false;
    graph.releaseResources();
}

void AudioEngine::setChannelGainDecibels (ChannelId id, float dB)
{
    if (auto* strip = strips[(size_t) id])
        strip->setGainDecibels (dB);
}

void AudioEngine::setChannelMute (ChannelId id, bool shouldMute)
{
    mutes[(size_t) id] = shouldMute;
    updateEffectiveMutes();
}

void AudioEngine::setChannelSolo (ChannelId id, bool shouldSolo)
{
    solos[(size_t) id] = shouldSolo;
    updateEffectiveMutes();
}

void AudioEngine::setMasterGainDecibels (float dB) { master->setGainDecibels (dB); }

void AudioEngine::updateEffectiveMutes()
{
    const bool anySolo = std::any_of (solos.begin(), solos.end(), [] (bool s) { return s; });

    const auto source = (size_t) ChannelId::source;
    for (size_t i = 0; i < strips.size(); ++i)
        if (auto* strip = strips[i])
        {
            if (bypassed) // the source alone, whatever the mixer says (restored when bypass ends)
                strip->setEffectiveMute (i != source);
            else
                strip->setEffectiveMute (mutes[i] || (anySolo && ! solos[i])
                                         || (i == source && stemsLoaded && ! solos[i])); // stems replace the source
        }

}

double AudioEngine::getOutputLatencyMs() const
{
    if (preparedSampleRate <= 0.0)
        return 0.0;

    int samples = preparedBlockSize + graph.getLatencySamples();

    if (auto* device = deviceManager.getCurrentAudioDevice())
        samples += device->getOutputLatencyInSamples();

    return 1000.0 * samples / preparedSampleRate;
}

} // namespace jaudios
