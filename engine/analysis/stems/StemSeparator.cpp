#include "StemSeparator.h"

#include "DemucsSpec.h"

#include "engine/analysis/Log.h"
#include "engine/analysis/Resampler.h"
#include "engine/util/Sha256.h"

#if JAUDIOS_HAS_ONNXRUNTIME
 #include <onnxruntime_cxx_api.h>
 #if JUCE_MAC
extern "C" OrtStatus* ORT_API_CALL OrtSessionOptionsAppendExecutionProvider_CoreML (OrtSessionOptions*, uint32_t flags);
 #endif
#endif

#include <cmath>
#include <vector>

namespace jaudios::analysis::stems
{

using G = DemucsGeometry;
constexpr double kExportParityThreshold = 1e-3; // ml/export_demucs.py PARITY

const char* stemName (Stem s) noexcept
{
    switch (s)
    {
        case Stem::drums:  return "drums";
        case Stem::bass:   return "bass";
        case Stem::other:  return "other";
        case Stem::vocals: return "vocals";
    }
    return "?";
}

std::optional<Stem> stemFromName (const juce::String& name)
{
    for (auto s : allStems)
        if (name.trim().equalsIgnoreCase (stemName (s)))
            return s;
    return std::nullopt;
}

juce::String StemSeparator::expectedModelSha256()
{
   #ifdef JAUDIOS_HTDEMUCS_ONNX_SHA256
    return JAUDIOS_HTDEMUCS_ONNX_SHA256;
   #else
    return {};
   #endif
}

std::optional<juce::String> verifyDemucsExport (const juce::File& model, const juce::String& actualSha, juce::String& why)
{
    const auto file = model.getSiblingFile (model.getFileName() + ".provenance.json");
    if (! file.existsAsFile())
    {
        why = "no " + file.getFileName();
        return std::nullopt;
    }
    const auto p = juce::JSON::parse (file);
    if (! p.isObject())
    {
        why = file.getFileName() + " is not valid JSON";
        return std::nullopt;
    }
    if (p["sha256"].toString() != actualSha)
    {
        why = "provenance sha256 does not match the file (modified or truncated after export)";
        return std::nullopt;
    }
   #ifdef JAUDIOS_DEMUCS_VERSION
    if (p["demucs_version"].toString() != JAUDIOS_DEMUCS_VERSION)
    {
        why = "exported with demucs " + p["demucs_version"].toString() + ", expected " JAUDIOS_DEMUCS_VERSION;
        return std::nullopt;
    }
   #endif
   #ifdef JAUDIOS_HTDEMUCS_CHECKPOINT_SHA256
    if (p["checkpoint_sha256"].toString() != JAUDIOS_HTDEMUCS_CHECKPOINT_SHA256)
    {
        why = "exported from a different checkpoint";
        return std::nullopt;
    }
   #endif
    const double parity = p.getProperty ("parity_max_abs", -1.0);
    const double threshold = p.getProperty ("parity_threshold", 0.0);
    if (! (parity >= 0.0 && threshold > 0.0 && threshold <= kExportParityThreshold && parity < threshold))
    {
        why = "provenance does not record a passing parity check";
        return std::nullopt;
    }
    return p["platform"].toString();
}

int StemSeparator::segmentCount (int64_t n, double overlap) noexcept
{
    const auto stride = (int64_t) ((1.0 - overlap) * G::segment);
    return n <= 0 || stride <= 0 ? 0 : (int) ((n + stride - 1) / stride);
}

//==============================================================================
struct StemSeparator::Impl
{
    DemucsSpec spec;
   #if JAUDIOS_HAS_ONNXRUNTIME
    std::unique_ptr<Ort::Session> session;
    Ort::RunOptions runOptions;

    static Ort::Env& env()
    {
        static Ort::Env e (ORT_LOGGING_LEVEL_WARNING, "JAudios");
        return e;
    }
   #endif
};

StemSeparator::StemSeparator (SeparatorConfig c) : config (std::move (c))
{
    impl = std::make_unique<Impl>();
   #if ! JAUDIOS_HAS_ONNXRUNTIME
    error = "built without ONNX Runtime (JAUDIOS_WITH_ONNXRUNTIME=OFF)";
   #else
    if (! config.modelFile.existsAsFile())
    {
        error = "stem model not found: " + config.modelFile.getFullPathName();
        log::warning ("Stem separation unavailable: " + error);
        return;
    }

    auto t0 = juce::Time::getMillisecondCounterHiRes();
    const juce::String got (util::Sha256::ofFile (config.modelFile.getFullPathName().toStdString()));
    if (const auto expected = expectedModelSha256(); expected.isNotEmpty() && got == expected)
        origin = "canonical";
    else
    {
        juce::String why;
        if (const auto platform = verifyDemucsExport (config.modelFile, got, why))
            origin = "local export (" + *platform + ")";
        else
        {
            error = "stem model sha256 " + got.substring (0, 12) + "... is not the manifest's and not a verified export: " + why;
            log::warning ("Stem separation unavailable: " + error);
            return;
        }
    }
    log::debug ("Stem model sha256 " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms (" + origin + ")");

    t0 = juce::Time::getMillisecondCounterHiRes();
    try
    {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (config.intraOpThreads > 0)
            options.SetIntraOpNumThreads (config.intraOpThreads);
        provider = "CPU";
       #if JUCE_MAC
        if (config.preferCoreML)
        {
            if (auto* status = OrtSessionOptionsAppendExecutionProvider_CoreML (options, 0); status == nullptr)
                provider = "CoreML";
            else
            {
                log::warning (juce::String ("CoreML execution provider unavailable: ") + Ort::GetApi().GetErrorMessage (status));
                Ort::GetApi().ReleaseStatus (status);
            }
        }
       #endif
       #if JUCE_WINDOWS
        impl->session = std::make_unique<Ort::Session> (Impl::env(), config.modelFile.getFullPathName().toWideCharPointer(), options);
       #else
        impl->session = std::make_unique<Ort::Session> (Impl::env(), config.modelFile.getFullPathName().toRawUTF8(), options);
       #endif
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("ONNX Runtime: ") + e.what();
        impl->session.reset();
        log::warning ("Stem separation unavailable: " + error);
    }
    log::debug ("Stem model session (" + provider + ") " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
   #endif
}

StemSeparator::~StemSeparator() = default;

bool StemSeparator::isReady() const noexcept
{
   #if JAUDIOS_HAS_ONNXRUNTIME
    return impl->session != nullptr;
   #else
    return false;
   #endif
}

void StemSeparator::cancel() noexcept
{
    cancelRequested.store (true);
   #if JAUDIOS_HAS_ONNXRUNTIME
    try { impl->runOptions.SetTerminate(); } catch (...) {}
   #endif
}

StemSeparator::Result StemSeparator::separate (const juce::AudioBuffer<float>& input, double sampleRate,
                                               const std::function<void (double)>& progress)
{
    Result result;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    cancelRequested.store (false);
   #if ! JAUDIOS_HAS_ONNXRUNTIME
    juce::ignoreUnused (input, sampleRate, progress);
    result.error = error;
    return result;
   #else
    if (! isReady())
    {
        result.error = error;
        return result;
    }
    if (input.getNumChannels() < 1 || input.getNumSamples() < 1 || sampleRate <= 0.0)
    {
        result.error = "empty input";
        return result;
    }
    impl->runOptions.UnsetTerminate();

    // demucs convert_audio: model channels (mono -> both, > 2 -> the first two), model rate
    juce::AudioBuffer<float> mix (G::channels, input.getNumSamples());
    for (int ch = 0; ch < G::channels; ++ch)
        mix.copyFrom (ch, 0, input, std::min (ch, input.getNumChannels() - 1), 0, input.getNumSamples());
    if (sampleRate != G::sampleRate)
        mix = resample (mix, sampleRate, G::sampleRate);
    const int n = mix.getNumSamples();

    // demucs.api: normalise by the mono reference (torch std is unbiased)
    double sum = 0.0, sq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double r = 0.5 * ((double) mix.getSample (0, i) + mix.getSample (1, i));
        sum += r;
        sq += r * r;
    }
    const double mean = sum / n;
    const double std = std::sqrt (n > 1 ? std::max (0.0, (sq - n * mean * mean) / (n - 1)) : 0.0) + 1e-8;
    for (int ch = 0; ch < G::channels; ++ch)
    {
        auto* x = mix.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            x[i] = (float) ((x[i] - mean) / std);
    }

    // apply_model(split=True, overlap, transition_power=1, shifts=0)
    const int stride = (int) ((1.0 - config.overlap) * G::segment);
    const int half = G::segment / 2;
    std::vector<float> weight ((size_t) G::segment);
    for (int i = 0; i < G::segment; ++i)
        weight[(size_t) i] = (float) (i < half ? i + 1 : G::segment - i) / (float) std::max (half, G::segment - half);

    std::vector<float> acc ((size_t) G::sources * G::channels * (size_t) n, 0.0f), sumWeight ((size_t) n, 0.0f);
    std::vector<float> mixSeg ((size_t) G::channels * G::segment), specIn ((size_t) G::specSize);
    std::vector<float> specOut ((size_t) G::sources * G::specSize), waveOut ((size_t) G::sources * G::channels * G::segment);

    auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 3> mixShape { 1, G::channels, G::segment };
    const std::array<int64_t, 4> specShape { 1, G::channels * 2, G::bins, G::frames };
    const std::array<int64_t, 5> specOutShape { 1, G::sources, G::channels * 2, G::bins, G::frames };
    const std::array<int64_t, 4> waveOutShape { 1, G::sources, G::channels, G::segment };
    std::array<Ort::Value, 2> inputs { Ort::Value::CreateTensor<float> (memory, mixSeg.data(), mixSeg.size(), mixShape.data(), mixShape.size()),
                                       Ort::Value::CreateTensor<float> (memory, specIn.data(), specIn.size(), specShape.data(), specShape.size()) };
    std::array<Ort::Value, 2> outputs { Ort::Value::CreateTensor<float> (memory, specOut.data(), specOut.size(), specOutShape.data(), specOutShape.size()),
                                        Ort::Value::CreateTensor<float> (memory, waveOut.data(), waveOut.size(), waveOutShape.data(), waveOutShape.size()) };
    const char* inNames[] = { "mix", "spec" };
    const char* outNames[] = { "spec_out", "wave_out" };

    const int total = segmentCount (n, config.overlap);
    for (int offset = 0; offset < n; offset += stride)
    {
        if (cancelRequested.load())
        {
            result.cancelled = true;
            break;
        }

        // TensorChunk(mix, offset, segment).padded(segment): centred, real audio around a short chunk
        const int chunk = std::min (n - offset, (int) G::segment);
        const int delta = G::segment - chunk;
        const int start = offset - delta / 2;
        for (int ch = 0; ch < G::channels; ++ch)
        {
            const float* x = mix.getReadPointer (ch);
            float* dst = mixSeg.data() + (size_t) ch * G::segment;
            for (int i = 0; i < G::segment; ++i)
                dst[i] = (start + i >= 0 && start + i < n) ? x[start + i] : 0.0f;
        }

        const float* stereo[] = { mixSeg.data(), mixSeg.data() + G::segment };
        impl->spec.forward (stereo, specIn.data());
        try
        {
            impl->session->Run (impl->runOptions, inNames, inputs.data(), inputs.size(), outNames, outputs.data(), outputs.size());
        }
        catch (const Ort::Exception& e)
        {
            if (cancelRequested.load())
                result.cancelled = true;
            else
                result.error = juce::String ("ONNX Runtime: ") + e.what();
            break;
        }

        // output = istft(spec branch) + time branch; then center_trim to the chunk and cross-fade
        for (int s = 0; s < G::sources; ++s)
        {
            float* wave = waveOut.data() + (size_t) s * G::channels * G::segment;
            float* outCh[] = { wave, wave + G::segment };
            impl->spec.inverseAdd (specOut.data() + (size_t) s * G::specSize, outCh);
            for (int ch = 0; ch < G::channels; ++ch)
            {
                const float* src = outCh[ch] + delta / 2;
                float* dst = acc.data() + ((size_t) (s * G::channels + ch)) * (size_t) n + (size_t) offset;
                for (int i = 0; i < chunk; ++i)
                    dst[i] += weight[(size_t) i] * src[i];
            }
        }
        for (int i = 0; i < chunk; ++i)
            sumWeight[(size_t) (offset + i)] += weight[(size_t) i];

        ++result.segments;
        if (progress)
            progress ((double) result.segments / total);
    }

    if (result.cancelled || result.error.isNotEmpty())
    {
        result.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
        return result;
    }

    Stems stems;
    stems.sampleRate = sampleRate;
    for (int s = 0; s < G::sources; ++s)
    {
        juce::AudioBuffer<float> b (G::channels, n);
        for (int ch = 0; ch < G::channels; ++ch)
        {
            const float* src = acc.data() + ((size_t) (s * G::channels + ch)) * (size_t) n;
            float* dst = b.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                dst[i] = (float) (src[i] / sumWeight[(size_t) i] * std + mean);
        }
        if (sampleRate != G::sampleRate)
        {
            auto back = resample (b, G::sampleRate, sampleRate);
            b.setSize (G::channels, input.getNumSamples(), false, true, false);
            b.clear();
            for (int ch = 0; ch < G::channels; ++ch)
                b.copyFrom (ch, 0, back, ch, 0, std::min (back.getNumSamples(), input.getNumSamples()));
        }
        stems.buffers[(size_t) s] = std::move (b);
    }
    result.stems = std::move (stems);
    result.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    log::debug ("Stem separation: " + juce::String (result.segments) + " segments in " + juce::String (result.seconds, 1) + " s");
    return result;
   #endif
}

} // namespace jaudios::analysis::stems
