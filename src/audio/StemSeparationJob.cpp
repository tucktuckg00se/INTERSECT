#include "StemSeparationJob.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <mutex>

#if INTERSECT_HAS_ONNX_RUNTIME
#define ORT_API_MANUAL_INIT
 #include <onnxruntime_cxx_api.h>
#undef ORT_API_MANUAL_INIT
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

void ensureOrtApiInitialized()
{
    static std::once_flag initOnce;
    std::call_once (initOnce, []
    {
        Ort::InitApi();
    });
}

Ort::Env& getOrtEnv()
{
    ensureOrtApiInitialized();
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

    // Accumulation buffers (allocated after first inference tells us stem count)
    int numOutputStems = 0;
    std::vector<juce::AudioBuffer<float>> accumBuffers;
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

        const int stemsInTensor = (int) shape[1];
        const float* outputData = outputValues.front().GetTensorData<float>();

        // Allocate accumulation buffers on first chunk
        if (accumBuffers.empty())
        {
            numOutputStems = stemsInTensor;
            accumBuffers.reserve ((size_t) numOutputStems);
            for (int s = 0; s < numOutputStems; ++s)
            {
                accumBuffers.emplace_back (2, fullLen);
                accumBuffers.back().clear();
            }
        }

        if (stemsInTensor != numOutputStems)
        {
            errorMessage = "Model output stem count changed between chunks";
            return false;
        }

        // Apply Hann window and accumulate
        // Output layout: [stem, channel, sample]
        const int stemStride = 2 * chunkSize;
        for (int s = 0; s < numOutputStems; ++s)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = outputData + (size_t) (s * stemStride + ch * chunkSize);
                float* dst = accumBuffers[(size_t) s].getWritePointer (ch, chunkStart);
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

    if (accumBuffers.empty())
    {
        errorMessage = "Stem model produced no output";
        return false;
    }

    // Normalize by accumulated window weight
    for (int s = 0; s < numOutputStems; ++s)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = accumBuffers[(size_t) s].getWritePointer (ch);
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
    for (int i = 0; i < catalog.numModelOutputs && i < 6; ++i)
        roles.push_back (catalog.modelOutputRoles[i]);
    if (catalog.computeResidual)
        roles.push_back (catalog.residualRole);
    return roles;
}

int countSelectedStemOutputs (StemSelectionMask selectionMask, int numOutputs)
{
    int count = 0;
    for (int i = 0; i < numOutputs; ++i)
        if (isStemOutputSelected (selectionMask, i))
            ++count;
    return count;
}

juce::String buildCombinedStemName (const std::vector<StemRole>& roles, StemSelectionMask selectionMask)
{
    juce::StringArray parts;
    for (int i = 0; i < (int) roles.size(); ++i)
    {
        if (! isStemOutputSelected (selectionMask, i))
            continue;

        parts.add (sanitisePathComponent (stemRoleToString (roles[(size_t) i])));
    }

    if (parts.isEmpty())
        return "stem";

    return parts.joinIntoString ("+");
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
                               const StemModelCatalogEntry& catalogEntry,
                               StemSelectionMask stemSelectionMask,
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
    jobModelId = modelId;
    jobCatalogEntry = catalogEntry;
    jobStemSelectionMask = stemSelectionMask;
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
    localResult.errorMessage = "ONNX Runtime is not bundled in this build";
    {
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
    }
    state.store (StemJobState::failed, std::memory_order_release);
    return;
#else
    StemJobState finalState = StemJobState::failed;

    if (jobCatalogEntry.numModelOutputs <= 0)
    {
        localResult.errorMessage = "Unknown stem model";
        const juce::ScopedLock sl (resultLock);
        result = std::move (localResult);
        state.store (StemJobState::failed, std::memory_order_release);
        return;
    }

    try
    {
        ensureOrtApiInitialized();

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
        if (! runWaveformChunked (session, inferenceAudio, jobCatalogEntry, stemBuffers,
                                  shouldCancel, *this, progress, parseError))
        {
            if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            {
                state.store (StemJobState::cancelled, std::memory_order_release);
                return;
            }

            throw std::runtime_error (parseError.toStdString());
        }

        const auto roles = buildStemRoles (jobCatalogEntry);
        if (roles.empty())
            throw std::runtime_error ("Stem model has no configured outputs");

        if ((int) stemBuffers.size() != (int) roles.size())
            throw std::runtime_error ("Stem model output count did not match catalog");

        if (countSelectedStemOutputs (jobStemSelectionMask, (int) roles.size()) <= 0)
            throw std::runtime_error ("Select at least one stem");

        state.store (StemJobState::writing, std::memory_order_release);

        juce::String writeError;
        if (jobExportMode == StemExportMode::separate)
        {
            int selectedStemCount = 0;
            for (size_t i = 0; i < stemBuffers.size(); ++i)
                if (isStemOutputSelected (jobStemSelectionMask, (int) i))
                    ++selectedStemCount;

            int writtenStemCount = 0;
            for (size_t i = 0; i < stemBuffers.size(); ++i)
            {
                if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
                {
                    state.store (StemJobState::cancelled, std::memory_order_release);
                    return;
                }

                if (! isStemOutputSelected (jobStemSelectionMask, (int) i))
                    continue;

                auto resampledStem = resampleStereoBuffer (stemBuffers[i], modelRate, jobSampleRate);
                const auto roleName = sanitisePathComponent (stemRoleToString (roles[i]));
                auto stemFile = jobOutputDir.getChildFile (jobSourceName + "_" + roleName + ".wav");

                if (! writeWaveFile (stemFile, resampledStem, jobSampleRate, writeError))
                    throw std::runtime_error (writeError.toStdString());

                localResult.stemFiles.push_back (stemFile);
                localResult.stemRoles.push_back (roles[i]);
                ++writtenStemCount;
                progress.store (0.7f + (0.3f * (float) writtenStemCount / (float) juce::jmax (1, selectedStemCount)),
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

                if (! isStemOutputSelected (jobStemSelectionMask, (int) i))
                    continue;

                for (int ch = 0; ch < 2; ++ch)
                    combinedStem.addFrom (ch, 0, stemBuffers[i], ch, 0, combinedStem.getNumSamples());

                progress.store (0.7f + (0.15f * (float) (i + 1) / (float) stemBuffers.size()),
                                std::memory_order_release);
            }

            auto resampledCombinedStem = resampleStereoBuffer (combinedStem, modelRate, jobSampleRate);
            const auto stemName = buildCombinedStemName (roles, jobStemSelectionMask);
            auto stemFile = jobOutputDir.getChildFile (jobSourceName + "_" + stemName + ".wav");

            if (! writeWaveFile (stemFile, resampledCombinedStem, jobSampleRate, writeError))
                throw std::runtime_error (writeError.toStdString());

            localResult.stemFiles.push_back (stemFile);
            localResult.stemRoles.push_back (StemRole::unknown);
            progress.store (1.0f, std::memory_order_release);
        }

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
