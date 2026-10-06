#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "StemSeparation.h"
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
    static constexpr int kMaxSessionSamples = 64;

    struct PeakMipmap
    {
        int samplesPerPeak = 0;
        std::vector<float> maxPeaks;
        std::vector<float> minPeaks;
    };

    static constexpr int kNumMipmapLevels = 3;

    struct SessionSample
    {
        int sampleId = 0;
        juce::String fileName;
        juce::String filePath;
        int startFrame = 0;
        int numFrames = 0;
        int sourceNumFrames = 0;
        double sourceSampleRate = 0.0;
        StemMetadata stemMeta;

        // REX2 loops only: musical position of each rendered slice, so the original
        // groove survives the transient-stretch tails. Re-derived from the file on
        // every decode, never serialized.
        struct BeatAnchor
        {
            int frame = 0;      // frame offset within this sample
            double beat = 0.0;  // quarter notes from the loop start
        };
        std::vector<BeatAnchor> beatAnchors;
        float anchorTempoBpm = 0.0f;
    };

    struct DecodedSample
    {
        juce::AudioBuffer<float> buffer;  // always stereo
        std::array<PeakMipmap, kNumMipmapLevels> peakMipmaps;
        juce::String fileName;
        juce::String filePath;
        int decodedNumFrames = 0;
        double decodedSampleRate = 0.0;
        int sourceNumFrames = 0;
        double sourceSampleRate = 0.0;
        std::vector<SessionSample> sessionSamples;

        // Populated only for REX2 files whose ids were passed as `importSliceSampleIds`
        // (freshly loaded files): the slice boundaries embedded in the REX2 metadata, as
        // absolute frame offsets into `buffer`. Reloads of existing session samples
        // (append, reorder, undo, state restore) keep their slices and import none.
        struct ImportedSlice
        {
            int sampleId = 0;
            int startSample = 0;
            int endSample   = 0;
        };
        std::vector<ImportedSlice> importedSlices;
        float importedTempoBpm = 0.0f;   // loop tempo of the first imported REX2 file
        std::vector<int> importSliceSampleIds;   // carried so a sample-rate retry imports the same set
    };

    using SnapshotPtr = std::shared_ptr<const DecodedSample>;

    SampleData();

    static std::unique_ptr<DecodedSample> decodeFromFile (const juce::File& file,
                                                           double projectSampleRate);
    static std::unique_ptr<DecodedSample> decodeFromFiles (const std::vector<juce::File>& files,
                                                           double projectSampleRate,
                                                           const std::vector<int>* sampleIds = nullptr,
                                                           const std::vector<int>* importSliceSampleIds = nullptr);
    static std::unique_ptr<DecodedSample> rebuildWithSessionSamples (const DecodedSample& source,
                                                                     const std::vector<SessionSample>& sessionSamples);

    // Audio-thread safe: converts unique_ptr to shared_ptr (no buffer copy).
    void applyDecodedSample (std::unique_ptr<DecodedSample> decoded);

    void clear();

    // Thread-safe snapshot for UI access.
    SnapshotPtr getSnapshot() const;

    // Audio-thread access — reads from the active decoded sample using linear interpolation.
    float getInterpolatedSample (double pos, int channel) const;
    float getSampleAtFrame (int frame, int channel) const;
    static float interpolateCubic (float y0, float y1, float y2, float y3, float frac);

    int getNumFrames() const { return numFrames.load (std::memory_order_acquire); }
    bool isLoaded() const { return loaded.load (std::memory_order_acquire); }
    double getDecodedSampleRate() const { return decodedSampleRate.load (std::memory_order_acquire); }
    int getSourceNumFrames() const { return sourceNumFrames.load (std::memory_order_acquire); }
    double getSourceSampleRate() const { return sourceSampleRate.load (std::memory_order_acquire); }
    int getNumSessionSamples() const;
    const SessionSample* findSessionSampleById (int sampleId) const;

    // REX2 slices to create for the active sample (empty unless a fresh REX2 load).
    // Audio-thread only, like getSessionSamples().
    const std::vector<DecodedSample::ImportedSlice>& getImportedSlices() const;

    // Audio-thread only — returns the buffer from the active decoded sample.
    const juce::AudioBuffer<float>& getBuffer() const;

    // Audio-thread only — returns mipmaps from the active decoded sample.
    const std::array<PeakMipmap, kNumMipmapLevels>& getMipmaps() const;

    const std::vector<SessionSample>& getSessionSamples() const;

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
    std::atomic<double> decodedSampleRate { 0.0 };
    std::atomic<int> sourceNumFrames { 0 };
    std::atomic<double> sourceSampleRate { 0.0 };

};
