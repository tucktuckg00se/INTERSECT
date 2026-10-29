#include "StemSeparationJob.h"
#include "StemSpectral.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <mutex>
#include <stdexcept>

#if INTERSECT_HAS_ONNX_RUNTIME
 #if JUCE_WINDOWS
  #include <windows.h>
 #else
  #include <dlfcn.h>
 #endif
 #define ORT_API_MANUAL_INIT
  #include <onnxruntime_cxx_api.h>
 #undef ORT_API_MANUAL_INIT
 #include <unordered_map>
#endif

namespace
{

juce::AudioBuffer<float> resampleStereoBuffer (const juce::AudioBuffer<float>& source,
                                               double sourceSampleRate,
                                               double targetSampleRate)
{
    if (sourceSampleRate <= 0.0 || targetSampleRate <= 0.0
        || std::abs (sourceSampleRate - targetSampleRate) <= 0.01)
    {
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf (source);
        return copy;
    }

    const double ratio = sourceSampleRate / targetSampleRate;
    const int sourceFrames = source.getNumSamples();
    const int targetFrames = juce::jmax (1, (int) std::ceil ((double) sourceFrames / ratio));
    juce::AudioBuffer<float> resampled (source.getNumChannels(), targetFrames);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio,
                              source.getReadPointer (ch),
                              resampled.getWritePointer (ch),
                              targetFrames);
    }

    return resampled;
}

bool writeWaveFile (const juce::File& file,
                    const juce::AudioBuffer<float>& audio,
                    double sampleRate,
                    juce::String& errorMessage)
{
    if (! file.getParentDirectory().createDirectory())
    {
        errorMessage = "Failed to create stem output folder";
        return false;
    }

    auto output = file.createOutputStream();
    if (output == nullptr)
    {
        errorMessage = "Failed to open stem output file";
        return false;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (output.release(),
                             sampleRate,
                             (unsigned int) audio.getNumChannels(),
                             24,
                             {},
                             0));
    if (writer == nullptr)
    {
        errorMessage = "Failed to create WAV writer";
        return false;
    }

    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
    {
        errorMessage = "Failed to write stem audio";
        return false;
    }

    return true;
}

juce::String sanitisePathComponent (juce::String text)
{
    text = text.trim();
    if (text.isEmpty())
        return "stem";

    text = text.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    return text.isEmpty() ? "stem" : text;
}

#if INTERSECT_HAS_ONNX_RUNTIME

struct OrtLoadState
{
    bool loaded = false;
    juce::String errorMessage;
};

bool loadOrtSharedLibrary (const juce::File& libraryFile, juce::String& errorMessage)
{
    if (libraryFile == juce::File() || ! libraryFile.existsAsFile())
    {
        errorMessage = "No ONNX Runtime bundle installed. "
                       "Download one in SET → Stem Separation → ONNX Runtime.";
        return false;
    }

    using OrtGetApiBaseFn = const OrtApiBase* (ORT_API_CALL*)();
    OrtGetApiBaseFn getApiBase = nullptr;

   #if JUCE_WINDOWS
    const std::wstring path = libraryFile.getFullPathName().toWideCharPointer();
    HMODULE module = ::LoadLibraryExW (path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr)
    {
        errorMessage = "Failed to load ONNX Runtime library: " + libraryFile.getFullPathName();
        return false;
    }
    getApiBase = reinterpret_cast<OrtGetApiBaseFn> (::GetProcAddress (module, "OrtGetApiBase"));
   #else
    // RTLD_GLOBAL so that lazily-loaded provider .so/.dylib files can resolve
    // their symbols back into the core runtime library.
    const auto path = libraryFile.getFullPathName().toStdString();
    void* handle = ::dlopen (path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr)
    {
        const char* err = ::dlerror();
        errorMessage = juce::String ("dlopen failed: ") + (err != nullptr ? err : "unknown error");
        return false;
    }
    getApiBase = reinterpret_cast<OrtGetApiBaseFn> (::dlsym (handle, "OrtGetApiBase"));
   #endif

    if (getApiBase == nullptr)
    {
        errorMessage = "OrtGetApiBase missing from " + libraryFile.getFileName();
        return false;
    }

    const OrtApiBase* apiBase = getApiBase();
    if (apiBase == nullptr)
    {
        errorMessage = "OrtGetApiBase returned null";
        return false;
    }

    const OrtApi* api = apiBase->GetApi (ORT_API_VERSION);
    if (api == nullptr)
    {
        errorMessage = "ONNX Runtime at " + libraryFile.getFullPathName()
                     + " is not compatible with the version this build of INTERSECT was compiled against";
        return false;
    }

    Ort::InitApi (api);
    return true;
}

bool ensureOrtApiInitialized (juce::String& errorMessage)
{
    static OrtLoadState state;
    static std::once_flag onceFlag;

    std::call_once (onceFlag, []
    {
        const auto root = getDefaultOrtRootFolder();
        const auto libFile = resolveActiveOrtLibraryFile (root);
        juce::String message;
        state.loaded = loadOrtSharedLibrary (libFile, message);
        state.errorMessage = message;
    });

    if (! state.loaded)
        errorMessage = state.errorMessage;

    return state.loaded;
}

Ort::Env& getOrtEnv()
{
    juce::String unused;
    (void) ensureOrtApiInitialized (unused);
    static Ort::Env env (ORT_LOGGING_LEVEL_WARNING, "INTERSECT");
    return env;
}

bool configureExecutionProvider (Ort::SessionOptions& options, StemComputeDevice requestedDevice, juce::String& errorMessage)
{
    if (requestedDevice == StemComputeDevice::cpu)
        return true;

    const auto providerName = getAvailableGpuProviderName();
    if (providerName.isEmpty())
    {
        errorMessage = "Installed ONNX Runtime bundle is CPU-only — no GPU provider is available";
        return false;
    }

    try
    {
        // ORT's generic string-name AppendExecutionProvider only accepts a
        // small whitelist (DML, QNN, XNNPACK, CoreML, ...). CUDA and MIGraphX
        // are NOT in that list — they must go through their typed APIs.
        if (providerName == "CUDA")
        {
            OrtCUDAProviderOptions cudaOptions;
            cudaOptions.device_id = 0;
            options.AppendExecutionProvider_CUDA (cudaOptions);
        }
        else if (providerName == "MIGraphX")
        {
            OrtMIGraphXProviderOptions migraphxOptions;
            migraphxOptions.device_id = 0;
            options.AppendExecutionProvider_MIGraphX (migraphxOptions);
        }
        else if (providerName == "DML")
        {
            // DirectML requires sequential execution and mem-patterns disabled.
            options.DisableMemPattern();
            options.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
            std::unordered_map<std::string, std::string> providerOptions;
            options.AppendExecutionProvider ("DML", providerOptions);
        }
        else
        {
            std::unordered_map<std::string, std::string> providerOptions;
            options.AppendExecutionProvider (providerName.toStdString(), providerOptions);
        }

        return true;
    }
    catch (const std::exception& e)
    {
        errorMessage = providerName + " unavailable: " + e.what();
        return false;
    }
}

// ── Waveform overlap-add inference ──────────────────────────────────────

std::vector<float> makeHannWindow (int size)
{
    std::vector<float> w ((size_t) size);
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    for (int i = 0; i < size; ++i)
        w[(size_t) i] = 0.5f * (1.0f - (float) std::cos (twoPi * (double) i / (double) size));
    return w;
}

bool runWaveformChunked (Ort::Session& session,
                         const juce::AudioBuffer<float>& sourceAudio,
                         const StemModelCatalogEntry& catalog,
                         const StemSelection& selection,
                         std::vector<juce::AudioBuffer<float>>& stemBuffers,
                         std::atomic<bool>& shouldCancel,
                         juce::Thread& thread,
                         std::atomic<float>& progress,
                         juce::String& errorMessage)
{
    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = session.GetInputNameAllocated (0, allocator);
    auto outputName = session.GetOutputNameAllocated (0, allocator);
    const char* inputNames[] = { inputName.get() };
    const char* outputNames[] = { outputName.get() };
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    const int originalLen = sourceAudio.getNumSamples();
    const int chunkSize = catalog.chunkSize;
    const int hopSize = juce::jmax (1, (int) ((float) chunkSize * (1.0f - catalog.overlapRatio)));
    const auto hannWindow = makeHannWindow (chunkSize);
    const int numModelStems = catalog.getNumOutputs();

    // Pad audio so that every sample is covered by at least one full chunk.
    // Add chunkSize - hopSize padding at both start and end, then round up to hop alignment.
    const int border = chunkSize - hopSize;
    const int paddedLen = originalLen + 2 * border;
    const int totalChunks = juce::jmax (1, (paddedLen - chunkSize) / hopSize + 1);
    const int fullLen = (totalChunks - 1) * hopSize + chunkSize;

    juce::AudioBuffer<float> paddedAudio (2, fullLen);
    paddedAudio.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        const int srcCh = juce::jmin (ch, sourceAudio.getNumChannels() - 1);
        paddedAudio.copyFrom (ch, border, sourceAudio, srcCh, 0, originalLen);
    }

    // Preallocate input buffer for the model: [1, 2, chunkSize]
    std::vector<float> inputBuffer ((size_t) (2 * chunkSize));
    const std::array<int64_t, 3> inputShape = { 1, 2, (int64_t) chunkSize };

    // Only selected stems are accumulated: a 53-stem model would otherwise need several GB
    // for a song-length sample.
    std::vector<juce::AudioBuffer<float>> accumBuffers;
    accumBuffers.reserve (selection.size());
    for (size_t s = 0; s < selection.size(); ++s)
    {
        accumBuffers.emplace_back (2, fullLen);
        accumBuffers.back().clear();
    }
    std::vector<float> weightAccum ((size_t) fullLen, 0.0f);

    for (int chunkIdx = 0; chunkIdx < totalChunks; ++chunkIdx)
    {
        if (thread.threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            return false;

        const int chunkStart = chunkIdx * hopSize;

        // Fill input tensor: [1, 2, chunkSize] — channel-first layout
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = paddedAudio.getReadPointer (ch, chunkStart);
            float* dst = inputBuffer.data() + (size_t) (ch * chunkSize);
            std::copy (src, src + chunkSize, dst);
        }

        auto inputTensor = Ort::Value::CreateTensor<float> (memoryInfo,
                                                             inputBuffer.data(),
                                                             inputBuffer.size(),
                                                             inputShape.data(),
                                                             inputShape.size());

        auto outputValues = session.Run (Ort::RunOptions { nullptr },
                                          inputNames, &inputTensor, 1,
                                          outputNames, 1);

        if (outputValues.empty() || ! outputValues.front().IsTensor())
        {
            errorMessage = "Stem model returned no output tensor";
            return false;
        }

        // Expected output shape: [1, numStems, 2, chunkSize]
        auto tensorInfo = outputValues.front().GetTensorTypeAndShapeInfo();
        auto shape = tensorInfo.GetShape();
        if (shape.size() != 4 || shape[0] != 1 || shape[2] != 2 || shape[3] != (int64_t) chunkSize)
        {
            errorMessage = "Unexpected model output shape (expected [1, stems, 2, " + juce::String (chunkSize) + "])";
            return false;
        }

        if (shape[1] != (int64_t) numModelStems)
        {
            errorMessage = "Stem model output count did not match catalog";
            return false;
        }

        const float* outputData = outputValues.front().GetTensorData<float>();

        // Apply Hann window and accumulate
        // Output layout: [stem, channel, sample]
        const size_t stemStride = (size_t) (2 * chunkSize);
        for (size_t s = 0; s < selection.size(); ++s)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = outputData + (size_t) selection[s] * stemStride + (size_t) (ch * chunkSize);
                float* dst = accumBuffers[s].getWritePointer (ch, chunkStart);
                for (int i = 0; i < chunkSize; ++i)
                    dst[i] += src[i] * hannWindow[(size_t) i];
            }
        }

        // Accumulate window weights
        for (int i = 0; i < chunkSize; ++i)
            weightAccum[(size_t) (chunkStart + i)] += hannWindow[(size_t) i];

        progress.store (0.15f + 0.55f * ((float) (chunkIdx + 1) / (float) totalChunks),
                        std::memory_order_release);
    }

    // Normalize by accumulated window weight
    for (auto& accum : accumBuffers)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = accum.getWritePointer (ch);
            for (int i = 0; i < fullLen; ++i)
            {
                const float w = weightAccum[(size_t) i];
                if (w > 1.0e-8f)
                    dst[i] /= w;
            }
        }
    }

    // Trim padding to recover original length
    stemBuffers.clear();
    stemBuffers.reserve (accumBuffers.size());
    for (const auto& accum : accumBuffers)
    {
        juce::AudioBuffer<float> stem (2, originalLen);
        for (int ch = 0; ch < 2; ++ch)
            stem.copyFrom (ch, 0, accum, ch, border, originalLen);
        stemBuffers.push_back (std::move (stem));
    }

    progress.store (0.7f, std::memory_order_release);
    return true;
}

// Spectral models: the host does the STFT. Mirrors the reference pipelines these models were
// verified against: optionally the mix normalized by its (mono) mean and deviation, demix()-style
// segments, outputs scaled back by the deviation.
//   spectral:     [1, 4, bins, frames] planes (L re, L im, R re, R im) in,
//                 [1, stems * 4, bins, frames] stem spectra out (same planes per stem)
//   spectralMask: [1, frames, bins * 4] (per frame and bin: L re, L im, R re, R im) in,
//                 [1, stems, frames, maskBins, 2 channels, 2 (re, im)] masks out, multiplied onto a
//                 maskNFft STFT of the mix
bool runSpectralSegmented (Ort::Session& session,
                           const juce::AudioBuffer<float>& sourceAudio,
                           const StemModelCatalogEntry& catalog,
                           const StemSelection& selection,
                           std::vector<juce::AudioBuffer<float>>& stemBuffers,
                           std::atomic<bool>& shouldCancel,
                           juce::Thread& thread,
                           std::atomic<float>& progress,
                           juce::String& errorMessage)
{
    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = session.GetInputNameAllocated (0, allocator);
    auto outputName = session.GetOutputNameAllocated (0, allocator);
    const char* inputNames[] = { inputName.get() };
    const char* outputNames[] = { outputName.get() };
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);

    const auto& params = catalog.spectral;
    const bool masked = catalog.runtimeKind == StemRuntimeKind::spectralMask;
    const int segmentSamples = params.segmentSamples;
    const int signalSamples = getStemSpectralSignalSamples (params);
    const int numStems = catalog.getNumOutputs();

    StemStft stft (params.nFft, params.hopSize, params.hannWindow, params.normalized, signalSamples);
    std::unique_ptr<StemStft> maskStft;
    if (masked)
        maskStft = std::make_unique<StemStft> (params.maskNFft, params.hopSize, params.hannWindow,
                                               params.normalized, signalSamples);

    const int bins = stft.getNumBins();
    const int frames = stft.getNumFrames();
    const size_t plane = stft.getPlaneSize();

    // Mean and sample deviation of the mono mix, in double.
    const int numSamples = sourceAudio.getNumSamples();
    double mean = 0.0, deviation = 1.0;
    if (params.normalizeInput)
    {
        double sum = 0.0, sumSq = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            const double mono = 0.5 * ((double) sourceAudio.getSample (0, i) + (double) sourceAudio.getSample (1, i));
            sum += mono;
            sumSq += mono * mono;
        }
        mean = sum / juce::jmax (1, numSamples);
        const double variance = numSamples > 1 ? (sumSq - (double) numSamples * mean * mean) / (double) (numSamples - 1) : 0.0;
        deviation = std::sqrt (juce::jmax (0.0, variance)) + 1.0e-8;
    }

    juce::AudioBuffer<float> normalised (2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = sourceAudio.getReadPointer (ch);
        float* dst = normalised.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            dst[i] = (float) (((double) src[i] - mean) / deviation);
    }

    // Planar STFT of both channels: L re, L im, R re, R im.
    std::vector<float> spectrum (4 * plane);
    std::vector<float> inputBuffer (4 * plane);
    std::array<int64_t, 4> inputShape4 = { 1, 4, (int64_t) bins, (int64_t) frames };
    std::array<int64_t, 3> inputShape3 = { 1, (int64_t) frames, (int64_t) bins * 4 };

    const int maskBins = masked ? maskStft->getNumBins() : 0;
    const size_t maskPlane = masked ? maskStft->getPlaneSize() : 0;
    std::vector<float> mixMaskSpectrum (4 * maskPlane);
    std::vector<float> stemRe (masked ? maskPlane : 0), stemIm (masked ? maskPlane : 0);

    auto processSegment = [&] (const juce::AudioBuffer<float>& segment,
                               std::vector<juce::AudioBuffer<float>>& outputs,
                               int segmentIndex, int numSegments) -> bool
    {
        if (thread.threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            return false;

        for (int ch = 0; ch < 2; ++ch)
            stft.forward (segment.getReadPointer (ch), segmentSamples,
                          spectrum.data() + (size_t) (2 * ch) * plane,
                          spectrum.data() + (size_t) (2 * ch + 1) * plane);

        if (masked)
        {
            // [frame][bin][L re, L im, R re, R im]
            for (int t = 0; t < frames; ++t)
                for (int k = 0; k < bins; ++k)
                    for (int c = 0; c < 4; ++c)
                        inputBuffer[((size_t) t * (size_t) bins + (size_t) k) * 4 + (size_t) c]
                            = spectrum[(size_t) c * plane + (size_t) k * (size_t) frames + (size_t) t];
        }
        else
        {
            std::copy (spectrum.begin(), spectrum.end(), inputBuffer.begin());
        }

        auto inputTensor = masked
            ? Ort::Value::CreateTensor<float> (memoryInfo, inputBuffer.data(), inputBuffer.size(),
                                               inputShape3.data(), inputShape3.size())
            : Ort::Value::CreateTensor<float> (memoryInfo, inputBuffer.data(), inputBuffer.size(),
                                               inputShape4.data(), inputShape4.size());

        auto outputValues = session.Run (Ort::RunOptions { nullptr },
                                          inputNames, &inputTensor, 1,
                                          outputNames, 1);

        if (outputValues.empty() || ! outputValues.front().IsTensor())
        {
            errorMessage = "Stem model returned no output tensor";
            return false;
        }

        const auto shape = outputValues.front().GetTensorTypeAndShapeInfo().GetShape();
        const float* outputData = outputValues.front().GetTensorData<float>();

        if (! masked)
        {
            if (shape.size() != 4 || shape[0] != 1 || shape[1] != (int64_t) (numStems * 4)
                || shape[2] != (int64_t) bins || shape[3] != (int64_t) frames)
            {
                errorMessage = "Unexpected model output shape (expected [1, " + juce::String (numStems * 4) + ", "
                             + juce::String (bins) + ", " + juce::String (frames) + "])";
                return false;
            }

            for (size_t s = 0; s < selection.size(); ++s)
            {
                const float* stem = outputData + (size_t) selection[s] * 4 * plane;
                for (int ch = 0; ch < 2; ++ch)
                    stft.inverse (stem + (size_t) (2 * ch) * plane, stem + (size_t) (2 * ch + 1) * plane,
                                  outputs[s].getWritePointer (ch), segmentSamples);
            }
        }
        else
        {
            // [1, stems, frames, maskBins, 2, 2], possibly with the last three axes flattened.
            int64_t perFrame = 1;
            for (size_t d = 3; d < shape.size(); ++d)
                perFrame *= shape[d];
            if (shape.size() < 4 || shape[0] != 1 || shape[1] != (int64_t) numStems || shape[2] != (int64_t) frames
                || perFrame != (int64_t) maskBins * 4)
            {
                errorMessage = "Unexpected model output shape (expected [1, " + juce::String (numStems) + ", "
                             + juce::String (frames) + ", " + juce::String (maskBins) + ", 2, 2])";
                return false;
            }

            for (int ch = 0; ch < 2; ++ch)
                maskStft->forward (segment.getReadPointer (ch), segmentSamples,
                                   mixMaskSpectrum.data() + (size_t) (2 * ch) * maskPlane,
                                   mixMaskSpectrum.data() + (size_t) (2 * ch + 1) * maskPlane);

            for (size_t s = 0; s < selection.size(); ++s)
            {
                const float* mask = outputData + (size_t) selection[s] * (size_t) frames * (size_t) maskBins * 4;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* mixRe = mixMaskSpectrum.data() + (size_t) (2 * ch) * maskPlane;
                    const float* mixIm = mixMaskSpectrum.data() + (size_t) (2 * ch + 1) * maskPlane;
                    for (int t = 0; t < frames; ++t)
                        for (int k = 0; k < maskBins; ++k)
                        {
                            const size_t m = (((size_t) t * (size_t) maskBins + (size_t) k) * 2 + (size_t) ch) * 2;
                            const size_t p = (size_t) k * (size_t) frames + (size_t) t;
                            const float mr = mask[m], mi = mask[m + 1];
                            stemRe[p] = mixRe[p] * mr - mixIm[p] * mi;
                            stemIm[p] = mixRe[p] * mi + mixIm[p] * mr;
                        }
                    maskStft->inverse (stemRe.data(), stemIm.data(), outputs[s].getWritePointer (ch), segmentSamples);
                }
            }
        }

        progress.store (0.15f + 0.55f * ((float) (segmentIndex + 1) / (float) numSegments),
                        std::memory_order_release);
        return true;
    };

    if (! runStemSegments (normalised, segmentSamples, params.strideSamples,
                           (int) selection.size(), processSegment, stemBuffers))
        return false;

    if (params.normalizeInput)
        for (auto& stem : stemBuffers)
            stem.applyGain ((float) deviation);

    progress.store (0.7f, std::memory_order_release);
    return true;
}

// Runs the model and returns one buffer per selected output, in selection order.
bool runStemInference (Ort::Session& session,
                       const juce::AudioBuffer<float>& sourceAudio,
                       const StemModelCatalogEntry& catalog,
                       const StemSelection& selection,
                       std::vector<juce::AudioBuffer<float>>& stemBuffers,
                       std::atomic<bool>& shouldCancel,
                       juce::Thread& thread,
                       std::atomic<float>& progress,
                       juce::String& errorMessage)
{
    switch (catalog.runtimeKind)
    {
        case StemRuntimeKind::waveform:
            return runWaveformChunked (session, sourceAudio, catalog, selection, stemBuffers,
                                       shouldCancel, thread, progress, errorMessage);
        case StemRuntimeKind::spectral:
        case StemRuntimeKind::spectralMask:
            return runSpectralSegmented (session, sourceAudio, catalog, selection, stemBuffers,
                                         shouldCancel, thread, progress, errorMessage);
    }

    errorMessage = "This stem model needs a newer version of INTERSECT";
    return false;
}

#endif // INTERSECT_HAS_ONNX_RUNTIME

juce::String buildCombinedStemName (const StemModelCatalogEntry& catalog, const StemSelection& selection)
{
    juce::StringArray parts;
    for (int index : selection)
        parts.add (sanitisePathComponent (catalog.outputs[(size_t) index].name));

    if (parts.isEmpty())
        return "stem";

    // Long combinations would make unwieldy file names.
    if (parts.size() > 4)
        return "mix-" + juce::String (parts.size()) + "-stems";

    return parts.joinIntoString ("+");
}

} // namespace

juce::String getStemGpuAvailabilityError()
{
    const auto providerName = getAvailableGpuProviderName();
    const auto providerLabel = providerName.isNotEmpty() ? providerName : juce::String ("GPU");

#if ! INTERSECT_HAS_ONNX_RUNTIME
    return providerLabel + " export unavailable. ONNX Runtime is not supported on this platform.";
#else
    try
    {
        juce::String initError;
        if (! ensureOrtApiInitialized (initError))
            return initError;

        (void) getOrtEnv();

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads (1);
        sessionOptions.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

        juce::String errorMessage;
        if (configureExecutionProvider (sessionOptions, StemComputeDevice::gpu, errorMessage))
            return {};

        if (errorMessage.isEmpty())
            errorMessage = providerLabel + " is not available in the installed runtime bundle.";

        return providerLabel + " export unavailable. INTERSECT could not start the "
             + providerLabel + " runtime on this system. Switch DEVICE to CPU or install the required "
             + providerLabel + " runtime. Details: " + errorMessage;
    }
    catch (const std::exception& e)
    {
        return providerLabel + " export unavailable: " + e.what();
    }
#endif
}

StemSeparationJob::StemSeparationJob()
    : juce::Thread ("StemSeparation")
{
}

StemSeparationJob::~StemSeparationJob()
{
    shouldCancel.store (true, std::memory_order_release);
    stopThread (10000);
}

bool StemSeparationJob::start (const juce::AudioBuffer<float>& sourceAudio,
                               double sampleRate,
                               int sampleId,
                               const juce::String& sourceName,
                               const StemModelCatalogEntry& catalogEntry,
                               const StemSelection& stemSelection,
                               StemExportMode exportMode,
                               StemComputeDevice computeDevice,
                               const juce::File& modelPath,
                               const juce::File& outputDir)
{
    if (isThreadRunning())
        return false;

    audioBuffer.makeCopyOf (sourceAudio);
    jobSampleRate = sampleRate;
    sourceSampleId.store (sampleId, std::memory_order_release);
    jobSourceName = sourceName;
    jobCatalogEntry = catalogEntry;
    jobStemSelection = normaliseStemSelection (stemSelection, catalogEntry.getNumOutputs());
    jobExportMode = exportMode;
    jobComputeDevice = computeDevice;
    jobModelPath = modelPath;
    jobOutputDir = outputDir;

    shouldCancel.store (false, std::memory_order_release);
    progress.store (0.0f, std::memory_order_release);

    {
        const juce::ScopedLock sl (resultLock);
        result = {};
    }

    state.store (StemJobState::preparing, std::memory_order_release);
    startThread (juce::Thread::Priority::background);
    return true;
}

StemJobResult StemSeparationJob::consumeResult()
{
    const juce::ScopedLock sl (resultLock);
    StemJobResult r = std::move (result);
    result = {};
    state.store (StemJobState::idle, std::memory_order_release);
    progress.store (0.0f, std::memory_order_release);
    sourceSampleId.store (-1, std::memory_order_release);
    return r;
}

void StemSeparationJob::cancel()
{
    shouldCancel.store (true, std::memory_order_release);
    signalThreadShouldExit();
}

void StemSeparationJob::run()
{
    StemJobResult localResult;

    if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
    {
        state.store (StemJobState::cancelled, std::memory_order_release);
        return;
    }

    if (! jobModelPath.existsAsFile())
    {
        localResult.errorMessage = "Selected stem model is not installed";
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
        state.store (StemJobState::failed, std::memory_order_release);
        return;
    }

#if ! INTERSECT_HAS_ONNX_RUNTIME
    localResult.errorMessage = "ONNX Runtime is not supported on this platform";
    {
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
    }
    state.store (StemJobState::failed, std::memory_order_release);
    return;
#else
    StemJobState finalState = StemJobState::failed;

    if (! jobCatalogEntry.isValid() || jobStemSelection.empty())
    {
        localResult.errorMessage = jobCatalogEntry.isValid() ? "Select at least one stem" : "Unknown stem model";
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
        state.store (StemJobState::failed, std::memory_order_release);
        return;
    }

    try
    {
        juce::String initError;
        if (! ensureOrtApiInitialized (initError))
            throw std::runtime_error (initError.toStdString());

        juce::String fallbackWarning;
        const double modelRate = jobCatalogEntry.sampleRate;
        juce::AudioBuffer<float> inferenceAudio = resampleStereoBuffer (audioBuffer, jobSampleRate, modelRate);
        if (inferenceAudio.getNumChannels() < 2)
        {
            juce::AudioBuffer<float> stereo (2, inferenceAudio.getNumSamples());
            stereo.copyFrom (0, 0, inferenceAudio, 0, 0, inferenceAudio.getNumSamples());
            stereo.copyFrom (1, 0, inferenceAudio, 0, 0, inferenceAudio.getNumSamples());
            inferenceAudio = std::move (stereo);
        }

        progress.store (0.1f, std::memory_order_release);
        state.store (StemJobState::separating, std::memory_order_release);

        Ort::SessionOptions sessionOptions;
       #if JUCE_WINDOWS
        sessionOptions.SetIntraOpNumThreads (juce::jlimit (1, 4, juce::SystemStats::getNumCpus()));
        sessionOptions.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
        sessionOptions.DisableMemPattern();
        sessionOptions.DisableCpuMemArena();
        sessionOptions.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_BASIC);
       #else
        sessionOptions.SetIntraOpNumThreads (juce::jmax (1, juce::SystemStats::getNumCpus() - 1));
        sessionOptions.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
       #endif

        juce::String providerError;
        if (! configureExecutionProvider (sessionOptions, jobComputeDevice, providerError))
        {
            if (jobComputeDevice == StemComputeDevice::gpu)
            {
                fallbackWarning = "GPU unavailable, used CPU instead: " + providerError;
                juce::Logger::writeToLog ("GPU provider failed, falling back to CPU: " + providerError);
            }
        }

       #if JUCE_WINDOWS
        const std::wstring modelPathStr = jobModelPath.getFullPathName().toWideCharPointer();
        Ort::Session session (getOrtEnv(), modelPathStr.c_str(), sessionOptions);
       #else
        const std::string modelPathStr = jobModelPath.getFullPathName().toStdString();
        Ort::Session session (getOrtEnv(), modelPathStr.c_str(), sessionOptions);
       #endif

        std::vector<juce::AudioBuffer<float>> stemBuffers;
        juce::String parseError;
        if (! runStemInference (session, inferenceAudio, jobCatalogEntry, jobStemSelection, stemBuffers,
                                shouldCancel, *this, progress, parseError))
        {
            if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            {
                state.store (StemJobState::cancelled, std::memory_order_release);
                return;
            }

            throw std::runtime_error (parseError.toStdString());
        }

        if (stemBuffers.size() != jobStemSelection.size())
            throw std::runtime_error ("Stem model output count did not match selection");

        state.store (StemJobState::writing, std::memory_order_release);

        juce::String writeError;
        if (jobExportMode == StemExportMode::separate)
        {
            for (size_t i = 0; i < stemBuffers.size(); ++i)
            {
                if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
                {
                    state.store (StemJobState::cancelled, std::memory_order_release);
                    return;
                }

                const auto& output = jobCatalogEntry.outputs[(size_t) jobStemSelection[i]];
                auto resampledStem = resampleStereoBuffer (stemBuffers[i], modelRate, jobSampleRate);
                auto stemFile = jobOutputDir.getChildFile (jobSourceName + "_" + sanitisePathComponent (output.name) + ".wav");

                if (! writeWaveFile (stemFile, resampledStem, jobSampleRate, writeError))
                    throw std::runtime_error (writeError.toStdString());

                localResult.stemFiles.push_back (stemFile);
                localResult.stemNames.push_back (output.name);
                progress.store (0.7f + (0.3f * (float) (i + 1) / (float) stemBuffers.size()),
                                std::memory_order_release);
            }
        }
        else
        {
            juce::AudioBuffer<float> combinedStem (2, inferenceAudio.getNumSamples());
            combinedStem.clear();

            for (size_t i = 0; i < stemBuffers.size(); ++i)
            {
                if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
                {
                    state.store (StemJobState::cancelled, std::memory_order_release);
                    return;
                }

                for (int ch = 0; ch < 2; ++ch)
                    combinedStem.addFrom (ch, 0, stemBuffers[i], ch, 0, combinedStem.getNumSamples());

                progress.store (0.7f + (0.15f * (float) (i + 1) / (float) stemBuffers.size()),
                                std::memory_order_release);
            }

            auto resampledCombinedStem = resampleStereoBuffer (combinedStem, modelRate, jobSampleRate);
            const auto stemName = buildCombinedStemName (jobCatalogEntry, jobStemSelection);
            auto stemFile = jobOutputDir.getChildFile (jobSourceName + "_" + stemName + ".wav");

            if (! writeWaveFile (stemFile, resampledCombinedStem, jobSampleRate, writeError))
                throw std::runtime_error (writeError.toStdString());

            localResult.stemFiles.push_back (stemFile);
            // A single selected output keeps its name; a real mix has no single instrument.
            localResult.stemNames.push_back (jobStemSelection.size() == 1
                                                 ? jobCatalogEntry.outputs[(size_t) jobStemSelection.front()].name
                                                 : stemName);
            progress.store (1.0f, std::memory_order_release);
        }

        localResult.warningMessage = fallbackWarning;

        finalState = StemJobState::completed;
    }
    catch (const std::exception& e)
    {
        localResult.errorMessage = juce::String (e.what());
        finalState = StemJobState::failed;
    }
    catch (...)
    {
        localResult.errorMessage = "Stem separation failed unexpectedly";
        finalState = StemJobState::failed;
    }

    {
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
    }
    state.store (finalState, std::memory_order_release);
#endif
}
