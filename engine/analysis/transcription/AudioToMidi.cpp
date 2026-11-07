#include "AudioToMidi.h"

#include "engine/analysis/Log.h"
#include "engine/analysis/Resampler.h"
#include "engine/util/Sha256.h"

#if JAUDIOS_HAS_ONNXRUNTIME
 #include <onnxruntime_cxx_api.h>
#endif

#include <algorithm>
#include <array>
#include <tuple>
#include <cmath>

namespace jaudios::analysis::transcription
{

using G = BasicPitchGeometry;

juce::String AudioToMidi::expectedModelSha256()
{
   #ifdef JAUDIOS_BASIC_PITCH_ONNX_SHA256
    return JAUDIOS_BASIC_PITCH_ONNX_SHA256;
   #else
    return {};
   #endif
}

//==============================================================================
juce::MidiMessageSequence toMidiSequence (const std::vector<Note>& input, bool multiplePitchBends)
{
    // drop_overlapping_pitch_bends: on notes sorted like Python tuples (start, end, pitch, ...)
    auto notes = input;
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) {
        return std::tie (a.startSec, a.endSec, a.pitch, a.confidence) < std::tie (b.startSec, b.endSec, b.pitch, b.confidence);
    });
    if (! multiplePitchBends)
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            for (size_t j = i + 1; j < notes.size(); ++j)
            {
                if (notes[j].startSec >= notes[i].endSec)
                    break;
                notes[i].bends.clear();
                notes[j].bends.clear();
            }

    juce::MidiMessageSequence seq;
    for (size_t idx = 0; idx < notes.size(); ++idx)
    {
        const auto& n = notes[idx];
        // one voice per channel when overlapping notes keep their own bends (channel 10 is drums)
        int channel = 1;
        if (multiplePitchBends)
        {
            channel = 1 + (int) (idx % 15);
            if (channel >= 10)
                ++channel;
        }
        seq.addEvent (juce::MidiMessage::noteOn (channel, n.pitch, (juce::uint8) std::clamp (n.velocity, 1, 127)).withTimeStamp (n.startSec));
        seq.addEvent (juce::MidiMessage::noteOff (channel, n.pitch).withTimeStamp (n.endSec));
        if (n.bends.empty())
            continue;
        const auto count = n.bends.size();
        for (size_t b = 0; b < count; ++b)
        {
            // np.linspace(start, end, len(bends)); ticks = round(bend * 4096 / 3), clipped to [-8192, 8191]
            const double t = count > 1 ? n.startSec + (n.endSec - n.startSec) * (double) b / (double) (count - 1) : n.startSec;
            const int ticks = std::clamp ((int) std::nearbyint (n.bends[b] * 4096.0 / 3.0), -8192, 8191);
            seq.addEvent (juce::MidiMessage::pitchWheel (channel, ticks + 8192).withTimeStamp (t));
        }
        // not in upstream: re-centre the wheel so the bend does not carry over to the next note
        seq.addEvent (juce::MidiMessage::pitchWheel (channel, 8192).withTimeStamp (n.endSec));
    }
    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

std::vector<Note> makeMonophonic (std::vector<Note> notes)
{
    constexpr double minKeepSec = 0.03;
    auto energy = [] (const Note& n) { return (double) n.confidence * (n.endSec - n.startSec); };
    std::stable_sort (notes.begin(), notes.end(), [&] (const Note& a, const Note& b) { return energy (a) > energy (b); });

    std::vector<Note> kept;
    for (auto n : notes)
    {
        const double origStart = n.startSec, origEnd = n.endSec;
        bool dropped = false;
        for (const auto& k : kept)
        {
            if (n.endSec <= k.startSec || n.startSec >= k.endSec)
                continue;
            const double before = k.startSec - n.startSec, after = n.endSec - k.endSec;
            if (before <= 0.0 && after <= 0.0) { dropped = true; break; } // fully covered
            if (before >= after) n.endSec = k.startSec;                     // keep the longer side
            else                 n.startSec = k.endSec;
            if (n.endSec - n.startSec < minKeepSec) { dropped = true; break; }
        }
        if (dropped)
            continue;
        if (! n.bends.empty() && (n.startSec != origStart || n.endSec != origEnd))
        {
            const auto len = (double) n.bends.size();
            const auto a = (size_t) std::clamp (std::lround ((n.startSec - origStart) / (origEnd - origStart) * len), 0L, (long) len);
            const auto b = (size_t) std::clamp (std::lround ((n.endSec - origStart) / (origEnd - origStart) * len), (long) a, (long) len);
            n.bends = std::vector<int> (n.bends.begin() + (long) a, n.bends.begin() + (long) b);
        }
        kept.push_back (std::move (n));
    }
    std::sort (kept.begin(), kept.end(), [] (const Note& a, const Note& b) { return std::tie (a.startSec, a.pitch) < std::tie (b.startSec, b.pitch); });
    return kept;
}

//==============================================================================
struct AudioToMidi::Impl
{
   #if JAUDIOS_HAS_ONNXRUNTIME
    std::unique_ptr<Ort::Session> session;
    static Ort::Env& env()
    {
        static Ort::Env e (ORT_LOGGING_LEVEL_WARNING, "JAudios");
        return e;
    }
   #endif
};

AudioToMidi::AudioToMidi (juce::File modelFile) : impl (std::make_unique<Impl>())
{
   #if ! JAUDIOS_HAS_ONNXRUNTIME
    juce::ignoreUnused (modelFile);
    error = "built without ONNX Runtime (JAUDIOS_WITH_ONNXRUNTIME=OFF)";
   #else
    if (! modelFile.existsAsFile())
    {
        error = "audio-to-MIDI model not found: " + modelFile.getFullPathName();
        log::warning ("Audio to MIDI unavailable: " + error);
        return;
    }
    // Upstream's own file, byte-for-byte: no local exports to accept, so the sha256 must match.
    const juce::String got (util::Sha256::ofFile (modelFile.getFullPathName().toStdString()));
    if (const auto expected = expectedModelSha256(); expected.isNotEmpty() && got != expected)
    {
        error = "audio-to-MIDI model sha256 mismatch (" + got.substring (0, 12) + "... != " + expected.substring (0, 12)
              + "...): run `python3 tools/models.py --only basic_pitch`";
        log::warning ("Audio to MIDI unavailable: " + error);
        return;
    }
    try
    {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
       #if JUCE_WINDOWS
        impl->session = std::make_unique<Ort::Session> (Impl::env(), modelFile.getFullPathName().toWideCharPointer(), options);
       #else
        impl->session = std::make_unique<Ort::Session> (Impl::env(), modelFile.getFullPathName().toRawUTF8(), options);
       #endif
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("ONNX Runtime: ") + e.what();
        log::warning ("Audio to MIDI unavailable: " + error);
    }
   #endif
}

AudioToMidi::~AudioToMidi() = default;

bool AudioToMidi::isReady() const noexcept
{
   #if JAUDIOS_HAS_ONNXRUNTIME
    return impl->session != nullptr;
   #else
    return false;
   #endif
}

Posteriorgram AudioToMidi::infer (const float* audio, int numSamples)
{
    Posteriorgram pg;
   #if JAUDIOS_HAS_ONNXRUNTIME
    if (! isReady() || numSamples <= 0)
        return pg;

    // get_audio_input: overlap_len / 2 zeros in front; windows every hop_size, zero-padded at the end
    const int lead = G::overlapSamples / 2;
    const int padded = numSamples + lead;
    const int numWindows = (padded + G::hopSamples - 1) / G::hopSamples;
    // unwrap_output: drop overlap/2 frames per side, concatenate, trim to floor(N * 86 / 22050)
    const int half = G::overlapFrames / 2;
    const int keep = G::windowFrames - 2 * half; // 142
    const int total = std::min (numWindows * keep, (int) std::floor (numSamples * ((double) G::framesPerSecond / G::sampleRate)));
    pg.frames = std::max (0, total);
    pg.note.assign ((size_t) pg.frames * G::noteBins, 0.0f);
    pg.onset.assign ((size_t) pg.frames * G::noteBins, 0.0f);
    pg.contour.assign ((size_t) pg.frames * G::contourBins, 0.0f);

    constexpr int batch = 8;
    std::vector<float> input ((size_t) batch * G::windowSamples);
    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const char* inNames[] = { "serving_default_input_2:0" };
    const char* outNames[] = { "StatefulPartitionedCall:1", "StatefulPartitionedCall:2", "StatefulPartitionedCall:0" }; // note, onset, contour

    for (int w0 = 0; w0 < numWindows; w0 += batch)
    {
        const int b = std::min (batch, numWindows - w0);
        for (int w = 0; w < b; ++w)
        {
            float* dst = input.data() + (size_t) w * G::windowSamples;
            const int start = (w0 + w) * G::hopSamples - lead; // in the unpadded signal
            for (int i = 0; i < G::windowSamples; ++i)
            {
                const int s = start + i;
                dst[i] = (s >= 0 && s < numSamples) ? audio[s] : 0.0f;
            }
        }
        const std::array<int64_t, 3> shape { b, G::windowSamples, 1 };
        auto tensor = Ort::Value::CreateTensor<float> (memory, input.data(), (size_t) b * G::windowSamples, shape.data(), shape.size());
        auto out = impl->session->Run (Ort::RunOptions { nullptr }, inNames, &tensor, 1, outNames, 3);

        const int binsOf[] = { G::noteBins, G::noteBins, G::contourBins };
        std::vector<float>* dstOf[] = { &pg.note, &pg.onset, &pg.contour };
        for (int o = 0; o < 3; ++o)
        {
            const float* src = out[(size_t) o].GetTensorData<float>();
            const int bins = binsOf[o];
            for (int w = 0; w < b; ++w)
                for (int t = 0; t < keep; ++t)
                {
                    const int frame = (w0 + w) * keep + t;
                    if (frame >= pg.frames)
                        break;
                    std::copy_n (src + ((size_t) w * G::windowFrames + (size_t) (t + half)) * bins, bins,
                                 dstOf[o]->data() + (size_t) frame * bins);
                }
        }
    }
   #else
    juce::ignoreUnused (audio, numSamples);
   #endif
    return pg;
}

Transcription AudioToMidi::transcribe (const juce::AudioBuffer<float>& audio, double sampleRate, const TranscriptionSettings& settings)
{
    Transcription r;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    if (! isReady())
    {
        r.error = error;
        return r;
    }
    if (audio.getNumChannels() < 1 || audio.getNumSamples() < 1 || sampleRate <= 0.0)
    {
        r.error = "empty input";
        return r;
    }

    // librosa.load(mono=True): mean of channels; then 22.05 kHz
    juce::AudioBuffer<float> mono (1, audio.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        mono.addFrom (0, 0, audio, ch, 0, audio.getNumSamples(), 1.0f / (float) audio.getNumChannels());
    if (sampleRate != G::sampleRate)
        mono = resample (mono, sampleRate, G::sampleRate);

    try
    {
        r.posteriors = infer (mono.getReadPointer (0), mono.getNumSamples());
    }
    catch (const std::exception& e)
    {
        r.error = juce::String ("ONNX Runtime: ") + e.what();
        return r;
    }

    for (auto& fn : extractNotes (r.posteriors, settings.notes))
    {
        Note n;
        n.startSec = frameToSeconds (fn.start);
        n.endSec = frameToSeconds (fn.end);
        n.pitch = fn.pitch;
        n.confidence = (float) fn.amplitude;
        n.velocity = (int) std::nearbyint (127.0 * fn.amplitude);
        if (settings.pitchBends)
            n.bends = std::move (fn.bends);
        r.notes.push_back (std::move (n));
    }
    if (settings.monophonic)
        r.notes = makeMonophonic (std::move (r.notes));
    else
        std::stable_sort (r.notes.begin(), r.notes.end(), [] (const Note& a, const Note& b) { return std::tie (a.startSec, a.pitch) < std::tie (b.startSec, b.pitch); });

    r.midi = toMidiSequence (r.notes, settings.multiplePitchBends);
    r.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    return r;
}

} // namespace jaudios::analysis::transcription
