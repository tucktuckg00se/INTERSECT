#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
#define INTERSECT_HAS_STD_ATOMIC_SHARED_PTR 1
#else
#define INTERSECT_HAS_STD_ATOMIC_SHARED_PTR 0
#endif

class SampleData
{
public:
    struct PeakMipmap
    {
        int samplesPerPeak = 0;
        std::vector<float> maxPeaks;
        std::vector<float> minPeaks;
    };

    static constexpr int kNumMipmapLevels = 3;

    struct DecodedSample
    {
        juce::AudioBuffer<float> buffer;  // always stereo
        std::array<PeakMipmap, kNumMipmapLevels> peakMipmaps;
        juce::String fileName;
        juce::String filePath;
    };

    using SnapshotPtr = std::shared_ptr<const DecodedSample>;

    SampleData();

    static std::unique_ptr<DecodedSample> decodeFromFile (const juce::File& file,
                                                           double projectSampleRate);

    // Audio-thread safe: converts unique_ptr to shared_ptr (no buffer copy).
    void applyDecodedSample (std::unique_ptr<DecodedSample> decoded);

    bool loadFromFile (const juce::File& file, double projectSampleRate);
    void clear();

    // Thread-safe snapshot for UI access.
    SnapshotPtr getSnapshot() const;

    // Audio-thread access — reads from the active decoded sample.
    float getInterpolatedSample (double pos, int channel) const;

    int getNumFrames() const { return numFrames.load (std::memory_order_acquire); }
    bool isLoaded() const { return loaded.load (std::memory_order_acquire); }

    // Audio-thread only — returns the buffer from the active decoded sample.
    const juce::AudioBuffer<float>& getBuffer() const;

    // Audio-thread only — returns mipmaps from the active decoded sample.
    const std::array<PeakMipmap, kNumMipmapLevels>& getMipmaps() const;

    const juce::String& getFileName() const { return loadedFileName; }
    void setFileName (const juce::String& name) { loadedFileName = name; }

    const juce::String& getFilePath() const { return loadedFilePath; }
    void setFilePath (const juce::String& path) { loadedFilePath = path; }

private:
    // Audio-thread-only strong reference to the current sample.
    // Written only on the audio thread (in applyDecodedSample / clear).
    std::shared_ptr<const DecodedSample> activeDecoded;

    // Atomic snapshot for thread-safe UI access (same object as activeDecoded).
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    std::atomic<std::shared_ptr<const DecodedSample>> snapshot;
#else
    std::shared_ptr<const DecodedSample> snapshot;
#endif

    // Atomic metadata for cross-thread queries.
    std::atomic<int> numFrames { 0 };
    std::atomic<bool> loaded { false };

    juce::String loadedFileName;
    juce::String loadedFilePath;
};
