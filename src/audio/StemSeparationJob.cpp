#include "StemSeparationJob.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

#if INTERSECT_HAS_ONNX_RUNTIME
 #include <onnxruntime_cxx_api.h>
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

Ort::Env& getOrtEnv()
{
    static Ort::Env env (ORT_LOGGING_LEVEL_WARNING, "INTERSECT");
    return env;
}

bool configureExecutionProvider (Ort::SessionOptions&, StemComputeDevice requestedDevice, juce::String& errorMessage)
{
    if (requestedDevice == StemComputeDevice::cpu)
        return true;

    errorMessage = "GPU inference is not available in this build";
    return false;
}

// ── BS-RoFormer STFT / inference helpers ─────────────────────────────────

std::vector<float> makeHannWindow (int size)
{
    std::vector<float> w ((size_t) size);
    const double twoPi = 2.0 * juce::MathConstants<double>::pi;
    for (int i = 0; i < size; ++i)
        w[(size_t) i] = 0.5f * (1.0f - (float) std::cos (twoPi * (double) i / (double) size));
    return w;
}

std::vector<float> makeFadeWindow (int chunkSize, int fadeSize)
{
    std::vector<float> w ((size_t) chunkSize, 1.0f);
    if (fadeSize <= 1)
        return w;
    for (int i = 0; i < fadeSize; ++i)
    {
        const float t = (float) i / (float) (fadeSize - 1);
        w[(size_t) i] = t;
        w[(size_t) (chunkSize - 1 - i)] = t;
    }
    return w;
}

void reflectPadChannel (const float* input, int inputLen, float* output, int padLeft, int padRight)
{
    if (inputLen >= 2 && padLeft < inputLen && padRight < inputLen)
    {
        for (int i = 0; i < padLeft; ++i)
            output[i] = input[padLeft - i];
        std::copy (input, input + inputLen, output + padLeft);
        for (int i = 0; i < padRight; ++i)
            output[padLeft + inputLen + i] = input[inputLen - 2 - i];
    }
    else
    {
        std::fill (output, output + padLeft, 0.0f);
        std::copy (input, input + inputLen, output + padLeft);
        std::fill (output + padLeft + inputLen, output + padLeft + inputLen + padRight, 0.0f);
    }
}

struct ForwardSTFTResult
{
    std::vector<float> modelInput;  // [T, 4 * freqBins] row-major
    std::vector<float> stftData;    // [2 * freqBins, T, 2] row-major (for mask multiply)
    int numFrames = 0;
};

// Forward STFT of stereo audio.
// modelInput: [numFrames, 4 * freqBins] with per-freq-bin interleaving:
//   [f0_ch0_re, f0_ch0_im, f0_ch1_re, f0_ch1_im, f1_ch0_re, ...]
// stftData: [2*freqBins, numFrames, 2] for complex mask multiplication.
//   chanFreq index cf = f * 2 + ch.
ForwardSTFTResult forwardSTFT (const juce::AudioBuffer<float>& audio,
                                int nFft, int hopLength,
                                const std::vector<float>& hannWindow)
{
    const int L = audio.getNumSamples();
    const int padSize = nFft / 2;
    const int paddedLen = L + nFft;
    const int numFrames = 1 + (paddedLen - nFft) / hopLength;
    const int freqBins = nFft / 2 + 1;
    const int modelFeatPerFrame = 4 * freqBins;
    const int chanFreqDim = 2 * freqBins;

    const int fftOrder = (int) std::round (std::log2 ((double) nFft));
    juce::dsp::FFT fft (fftOrder);
    const size_t fftBufSize = (size_t) (2 * nFft);
    std::vector<float> fftBuffer (fftBufSize);
    std::vector<float> padded ((size_t) paddedLen);

    ForwardSTFTResult out;
    out.numFrames = numFrames;
    out.modelInput.resize ((size_t) (numFrames * modelFeatPerFrame), 0.0f);
    out.stftData.resize ((size_t) (chanFreqDim * numFrames * 2), 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        reflectPadChannel (audio.getReadPointer (ch), L, padded.data(), padSize, padSize);

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const int frameStart = frame * hopLength;

            std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
            for (int i = 0; i < nFft; ++i)
                fftBuffer[(size_t) i] = padded[(size_t) (frameStart + i)] * hannWindow[(size_t) i];

            fft.performRealOnlyForwardTransform (fftBuffer.data(), true);

            for (int f = 0; f < freqBins; ++f)
            {
                const float re = fftBuffer[(size_t) (2 * f)];
                const float im = fftBuffer[(size_t) (2 * f + 1)];

                // Model input: interleaved per freq bin
                const size_t mIdx = (size_t) frame * modelFeatPerFrame
                                  + (size_t) (f * 4 + ch * 2);
                out.modelInput[mIdx]     = re;
                out.modelInput[mIdx + 1] = im;

                // STFT data: [cf, frame, 2] where cf = f*2 + ch
                const size_t sIdx = (size_t) (((f * 2 + ch) * numFrames + frame) * 2);
                out.stftData[sIdx]     = re;
                out.stftData[sIdx + 1] = im;
            }
        }
    }

    return out;
}

// Apply complex mask to saved STFT, then inverse STFT.
// maskData: model output [stems, 2*freqBins, T, 2] — complex mask.
//   chanFreq interleaved: [f0_ch0, f0_ch1, f1_ch0, f1_ch1, ...]
// stftData: [2*freqBins, T, 2] — saved from forwardSTFT, same interleaving.
// For each element: separated = stft * mask (complex multiply), then ISTFT.
std::vector<juce::AudioBuffer<float>> applyMaskAndISTFT (
    const float* maskData,
    const std::vector<float>& stftData,
    int numStems,
    int nFft, int hopLength,
    int freqBins, int numFrames,
    int originalLength,
    const std::vector<float>& hannWindow)
{
    const int fftOrder = (int) std::round (std::log2 ((double) nFft));
    juce::dsp::FFT fft (fftOrder);
    const int padSize = nFft / 2;
    const int paddedLen = originalLength + nFft;
    const size_t fftBufSize = (size_t) (2 * nFft);
    const int chanFreqDim = 2 * freqBins;

    std::vector<float> fftBuffer (fftBufSize);
    std::vector<float> outputBuf ((size_t) paddedLen);
    std::vector<float> windowSum ((size_t) paddedLen);

    std::vector<juce::AudioBuffer<float>> result;
    result.reserve ((size_t) numStems);

    for (int stem = 0; stem < numStems; ++stem)
    {
        juce::AudioBuffer<float> stereo (2, originalLength);
        stereo.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (outputBuf.begin(), outputBuf.end(), 0.0f);
            std::fill (windowSum.begin(), windowSum.end(), 0.0f);

            for (int frame = 0; frame < numFrames; ++frame)
            {
                const int frameStart = frame * hopLength;

                std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);

                for (int f = 0; f < freqBins; ++f)
                {
                    // chanFreq interleaved: cf = f * 2 + ch
                    const int cf = f * 2 + ch;

                    // Mask: [stem, cf, frame, 2]
                    const size_t maskIdx = (size_t) (((stem * chanFreqDim + cf) * numFrames + frame) * 2);
                    const float mask_re = maskData[maskIdx];
                    const float mask_im = maskData[maskIdx + 1];

                    // Saved STFT: [cf, frame, 2]
                    const size_t stftIdx = (size_t) ((cf * numFrames + frame) * 2);
                    const float stft_re = stftData[stftIdx];
                    const float stft_im = stftData[stftIdx + 1];

                    // Complex multiply: separated = stft * mask
                    fftBuffer[(size_t) (2 * f)]     = stft_re * mask_re - stft_im * mask_im;
                    fftBuffer[(size_t) (2 * f + 1)] = stft_re * mask_im + stft_im * mask_re;
                }

                // Fill conjugate mirror: bin[N-k] = conj(bin[k])
                for (int k = 1; k < freqBins - 1; ++k)
                {
                    fftBuffer[(size_t) (2 * (nFft - k))]     =  fftBuffer[(size_t) (2 * k)];
                    fftBuffer[(size_t) (2 * (nFft - k) + 1)] = -fftBuffer[(size_t) (2 * k + 1)];
                }

                fft.performRealOnlyInverseTransform (fftBuffer.data());

                for (int i = 0; i < nFft && frameStart + i < paddedLen; ++i)
                {
                    const float w = hannWindow[(size_t) i];
                    outputBuf[(size_t) (frameStart + i)] += fftBuffer[(size_t) i] * w;
                    windowSum[(size_t) (frameStart + i)] += w * w;
                }
            }

            for (int i = 0; i < paddedLen; ++i)
            {
                if (windowSum[(size_t) i] > 1.0e-8f)
                    outputBuf[(size_t) i] /= windowSum[(size_t) i];
            }

            stereo.copyFrom (ch, 0, outputBuf.data() + padSize, originalLength);
        }

        result.push_back (std::move (stereo));
    }

    return result;
}

bool runBsRoformerChunked (Ort::Session& session,
                            const juce::AudioBuffer<float>& sourceAudio,
                            const StemModelCatalogEntry& catalog,
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
    const int step = chunkSize / juce::jmax (1, catalog.numOverlap);
    const int border = chunkSize - step;
    const int paddedLen = originalLen + 2 * border;
    const int fadeSize = chunkSize / 10;

    juce::AudioBuffer<float> paddedAudio (2, paddedLen);
    for (int ch = 0; ch < 2; ++ch)
        reflectPadChannel (sourceAudio.getReadPointer (ch), originalLen,
                           paddedAudio.getWritePointer (ch), border, border);

    const auto hannWindow = makeHannWindow (catalog.nFft);
    const auto fadeWindow = makeFadeWindow (chunkSize, fadeSize);

    int totalChunks = 0;
    for (int pos = 0; pos < paddedLen; pos += step)
        ++totalChunks;
    if (totalChunks < 1)
        totalChunks = 1;

    int numOutputStems = 0;
    std::vector<juce::AudioBuffer<float>> accumBuffers;
    std::vector<float> counter ((size_t) paddedLen, 0.0f);

    int chunkIndex = 0;
    for (int chunkStart = 0; chunkStart < paddedLen; chunkStart += step)
    {
        if (thread.threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            return false;

        const int actualLen = juce::jmin (chunkSize, paddedLen - chunkStart);

        juce::AudioBuffer<float> chunk (2, chunkSize);
        chunk.clear();
        for (int ch = 0; ch < 2; ++ch)
            chunk.copyFrom (ch, 0, paddedAudio, ch, chunkStart, actualLen);

        auto stft = forwardSTFT (chunk, catalog.nFft, catalog.hopLength, hannWindow);

        const int freqBins = catalog.nFft / 2 + 1;
        const int featuresPerFrame = 4 * freqBins;
        const std::array<int64_t, 3> inputShape = { 1, (int64_t) stft.numFrames, (int64_t) featuresPerFrame };
        auto inputTensor = Ort::Value::CreateTensor<float> (memoryInfo,
                                                             stft.modelInput.data(),
                                                             stft.modelInput.size(),
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

        // Output shape: [1, stems, 2*freqBins, T, 2] — complex mask
        auto tensorInfo = outputValues.front().GetTensorTypeAndShapeInfo();
        auto shape = tensorInfo.GetShape();

        if (shape.size() != 5 || shape[0] != 1 || shape[4] != 2)
        {
            errorMessage = "Unexpected model output shape (expected [1, stems, chanFreq, T, 2])";
            return false;
        }

        const int stemsInTensor = (int) shape[1];
        const float* maskData = outputValues.front().GetTensorData<float>();

        auto stemChunks = applyMaskAndISTFT (maskData, stft.stftData, stemsInTensor,
                                              catalog.nFft, catalog.hopLength,
                                              freqBins, stft.numFrames, chunkSize, hannWindow);

        if (accumBuffers.empty())
        {
            numOutputStems = stemsInTensor;
            accumBuffers.reserve ((size_t) numOutputStems);
            for (int s = 0; s < numOutputStems; ++s)
            {
                accumBuffers.emplace_back (2, paddedLen);
                accumBuffers.back().clear();
            }
        }

        if (stemsInTensor != numOutputStems)
        {
            errorMessage = "Model output stem count changed between chunks";
            return false;
        }

        for (int s = 0; s < numOutputStems; ++s)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* dst = accumBuffers[(size_t) s].getWritePointer (ch, chunkStart);
                const float* src = stemChunks[(size_t) s].getReadPointer (ch);
                for (int i = 0; i < actualLen; ++i)
                    dst[i] += src[i] * fadeWindow[(size_t) i];
            }
        }

        for (int i = 0; i < actualLen; ++i)
            counter[(size_t) (chunkStart + i)] += fadeWindow[(size_t) i];

        ++chunkIndex;
        progress.store (0.15f + 0.55f * ((float) chunkIndex / (float) totalChunks),
                        std::memory_order_release);
    }

    if (accumBuffers.empty())
    {
        errorMessage = "Stem model produced no output";
        return false;
    }

    for (int s = 0; s < numOutputStems; ++s)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = accumBuffers[(size_t) s].getWritePointer (ch);
            for (int i = 0; i < paddedLen; ++i)
            {
                const float denom = counter[(size_t) i] > 1.0e-8f ? counter[(size_t) i] : 1.0f;
                dst[i] /= denom;
            }
        }
    }

    stemBuffers.clear();
    stemBuffers.reserve ((size_t) numOutputStems);
    for (int s = 0; s < numOutputStems; ++s)
    {
        juce::AudioBuffer<float> stem (2, originalLen);
        for (int ch = 0; ch < 2; ++ch)
            stem.copyFrom (ch, 0, accumBuffers[(size_t) s], ch, border, originalLen);
        stemBuffers.push_back (std::move (stem));
    }

    progress.store (0.7f, std::memory_order_release);
    return true;
}

#endif // INTERSECT_HAS_ONNX_RUNTIME

std::vector<StemRole> buildStemRoles (const StemModelCatalogEntry& catalog)
{
    std::vector<StemRole> roles;
    roles.reserve ((size_t) (catalog.numModelOutputs + (catalog.computeResidual ? 1 : 0)));
    for (int i = 0; i < catalog.numModelOutputs && i < 4; ++i)
        roles.push_back (catalog.modelOutputRoles[i]);
    if (catalog.computeResidual)
        roles.push_back (catalog.residualRole);
    return roles;
}

} // namespace

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
                               StemModelId modelId,
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
    jobModelId = modelId;
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
    localResult.errorMessage = "ONNX Runtime is not bundled in this build";
    {
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
    }
    state.store (StemJobState::failed, std::memory_order_release);
    return;
#else
    StemJobState finalState = StemJobState::failed;

    const auto* catalogEntry = findStemModelCatalogEntry (jobModelId);
    if (catalogEntry == nullptr)
    {
        localResult.errorMessage = "Unknown stem model";
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
        state.store (StemJobState::failed, std::memory_order_release);
        return;
    }

    try
    {
        const double modelRate = catalogEntry->sampleRate;
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
        sessionOptions.SetIntraOpNumThreads (juce::jmax (1, juce::SystemStats::getNumCpus() - 1));
        sessionOptions.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

        juce::String providerError;
        if (! configureExecutionProvider (sessionOptions, jobComputeDevice, providerError))
            throw std::runtime_error (providerError.toStdString());

       #if JUCE_WINDOWS
        const std::wstring modelPathStr = jobModelPath.getFullPathName().toWideCharPointer();
        Ort::Session session (getOrtEnv(), modelPathStr.c_str(), sessionOptions);
       #else
        const std::string modelPathStr = jobModelPath.getFullPathName().toStdString();
        Ort::Session session (getOrtEnv(), modelPathStr.c_str(), sessionOptions);
       #endif

        std::vector<juce::AudioBuffer<float>> stemBuffers;
        juce::String parseError;
        if (! runBsRoformerChunked (session, inferenceAudio, *catalogEntry, stemBuffers,
                                     shouldCancel, *this, progress, parseError))
        {
            if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            {
                state.store (StemJobState::cancelled, std::memory_order_release);
                return;
            }

            throw std::runtime_error (parseError.toStdString());
        }

        // Compute residual stem (e.g. instrumental = mix - vocals)
        if (catalogEntry->computeResidual)
        {
            const int numFrames = inferenceAudio.getNumSamples();
            juce::AudioBuffer<float> residual;
            residual.makeCopyOf (inferenceAudio);
            for (const auto& stem : stemBuffers)
                for (int ch = 0; ch < 2; ++ch)
                    residual.addFrom (ch, 0, stem, ch, 0, numFrames, -1.0f);
            stemBuffers.push_back (std::move (residual));
        }

        state.store (StemJobState::writing, std::memory_order_release);

        const auto roles = buildStemRoles (*catalogEntry);
        for (size_t i = 0; i < stemBuffers.size(); ++i)
        {
            if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            {
                state.store (StemJobState::cancelled, std::memory_order_release);
                return;
            }

            stemBuffers[i] = resampleStereoBuffer (stemBuffers[i], modelRate, jobSampleRate);

            juce::String stemName = sanitisePathComponent (
                stemRoleToString (i < roles.size() ? roles[i] : StemRole::unknown));
            auto stemFile = jobOutputDir.getChildFile (jobSourceName + "_" + stemName + ".wav");

            juce::String writeError;
            if (! writeWaveFile (stemFile, stemBuffers[i], jobSampleRate, writeError))
                throw std::runtime_error (writeError.toStdString());

            localResult.stemFiles.push_back (stemFile);
            localResult.stemRoles.push_back (i < roles.size() ? roles[i] : StemRole::unknown);
            progress.store (0.7f + (0.3f * (float) (i + 1) / (float) stemBuffers.size()),
                            std::memory_order_release);
        }

        finalState = StemJobState::completed;
    }
    catch (const std::exception& e)
    {
        localResult.errorMessage = juce::String (e.what());
        finalState = StemJobState::failed;
    }

    {
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
    }
    state.store (finalState, std::memory_order_release);
#endif
}
