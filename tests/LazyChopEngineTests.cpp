#include "audio/LazyChopEngine.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace
{
constexpr double sampleRate = 44100.0;
constexpr int sampleLength = 32768;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

float sineContent (int i)
{
    return (float) std::sin (juce::MathConstants<double>::twoPi * i / 100.0);
}

struct Fixture
{
    SampleData sample;
    SliceManager slices;
    VoicePool voices;
    LazyChopEngine chop;

    explicit Fixture (int algorithm, bool snap = false, float (*content) (int) = sineContent)
    {
        auto decoded = std::make_unique<SampleData::DecodedSample>();
        decoded->buffer.setSize (2, sampleLength);
        decoded->decodedSampleRate = sampleRate;
        for (int i = 0; i < sampleLength; ++i)
        {
            const auto value = content (i);
            decoded->buffer.setSample (0, i, value);
            decoded->buffer.setSample (1, i, value);
        }
        sample.applyDecodedSample (std::move (decoded));
        voices.prepareToPlay (sampleRate, 512);
        PreviewStretchParams params;
        params.stretchEnabled = true;
        params.algorithm = algorithm;
        params.bpm = 120.0f;
        params.dawBpm = 60.0f;
        params.sampleRate = sampleRate;
        params.sample = &sample;
        chop.start (sampleLength, slices, params, snap, &sample.getBuffer());
    }

    int playhead() const
    {
        return (int) std::floor (voices.voicePositions[VoicePool::kPreviewVoiceIndex].load());
    }

    float renderOne()
    {
        float left = 0.0f, right = 0.0f;
        voices.processSample (sample, sampleRate, left, right);
        require (std::isfinite (left) && std::isfinite (right), "audio must stay finite");
        return left;
    }

    double render (int frames)
    {
        double energy = 0.0;
        for (int i = 0; i < frames; ++i)
        {
            float left = 0.0f, right = 0.0f;
            voices.processSample (sample, sampleRate, left, right);
            require (std::isfinite (left) && std::isfinite (right), "audio must stay finite");
            energy += left * left + right * right;
        }
        return energy;
    }
};

void chopsAtTheAdvancingPlayhead (int algorithm)
{
    Fixture f (algorithm);
    require (f.chop.onNote (60, f.voices, f.slices) == -1, "first note starts playback");
    require (f.render (16384) > 1.0, "preview must render audible audio");
    const int firstEnd = f.playhead();
    require (firstEnd > 4096 && firstEnd < 16384, "preview must advance at the stretched tempo");
    const int first = f.chop.onNote (61, f.voices, f.slices);
    require (first >= 0, "next note must create a slice while audio is stretched");
    require (f.slices.getSlice (first).startSample == 0, "first slice starts at zero");
    require (f.slices.getSlice (first).endSample == firstEnd, "slice ends at the visible playhead");

    f.render (8192);
    const int secondEnd = f.playhead();
    const int second = f.chop.onNote (62, f.voices, f.slices);
    require (second >= 0, "another note must create the next slice");
    require (f.slices.getSlice (second).startSample == firstEnd, "slices must be adjacent");
    require (f.slices.getSlice (second).endSample == secondEnd, "second boundary follows playback");
}

void closesSlicesAcrossLoopWrap (int algorithm)
{
    Fixture f (algorithm);
    f.chop.onNote (60, f.voices, f.slices);
    f.render (16384);
    const int firstEnd = f.playhead();
    require (f.chop.onNote (61, f.voices, f.slices) >= 0, "create slice before wrapping");

    int previous = firstEnd;
    bool wrapped = false;
    for (int i = 0; i < sampleLength * 4; ++i)
    {
        f.render (1);
        const int current = f.playhead();
        if (current < previous)
        {
            wrapped = true;
            break;
        }
        previous = current;
    }
    require (wrapped, "preview must loop to the sample start");
    f.render (4096);
    const int end = f.playhead();
    require (end >= kMinSliceLengthSamples && end < firstEnd, "playhead must be after loop start");
    require (f.chop.onNote (62, f.voices, f.slices) >= 0, "chop after loop wrap");
    require (f.slices.getNumSlices() == 3, "wrap must close the tail and create a head slice");
    require (f.slices.getSlice (1).startSample == firstEnd, "tail starts at previous chop");
    require (f.slices.getSlice (1).endSample == sampleLength, "tail closes at sample end");
    require (f.slices.getSlice (2).startSample == 0, "head starts at zero");
    require (f.slices.getSlice (2).endSample == end, "head closes at wrapped playhead");
}

void auditionResetsTheStartWithoutRendering (int algorithm)
{
    Fixture f (algorithm);
    const int existing = f.slices.createSlice (5000, 10000);
    require (existing >= 0, "create audition slice");
    f.slices.getSlice (existing).midiNote = 36;
    f.slices.rebuildMidiMap();
    f.chop.onNote (60, f.voices, f.slices);
    f.render (16384);
    f.chop.onNote (36, f.voices, f.slices);
    require (f.chop.onNote (61, f.voices, f.slices) == -1, "first note after audition sets a start");
    require (f.chop.getChopPos() == 5000, "audition position is immediate, not a stale UI cursor");
    require (f.slices.getNumSlices() == 1, "audition must not create an extra slice");
}

void snapsStretchedBoundariesToZeroCrossings (int algorithm)
{
    Fixture f (algorithm, true);
    f.chop.onNote (60, f.voices, f.slices);
    f.render (16387);
    const int beforeSnap = f.playhead();
    const int idx = f.chop.onNote (61, f.voices, f.slices);
    require (idx >= 0, "snapped stretched chop must create a slice");
    const int end = f.slices.getSlice (idx).endSample;
    require (std::abs (end - beforeSnap) <= 26, "snap chooses a nearby sine zero crossing");
    const auto& buffer = f.sample.getBuffer();
    const float left = buffer.getSample (0, end - 1);
    const float right = buffer.getSample (0, end);
    require ((left >= 0.0f && right < 0.0f) || (left < 0.0f && right >= 0.0f),
             "slice boundary must be on a sign change");
}

void keepsSamplePrecisionForLongRecordings (int algorithm)
{
    // Exercise positions beyond float's exact integer range without allocating
    // or rendering a many-minute audio buffer.
    VoicePool voices;
    SliceManager slices;
    LazyChopEngine chop;
    constexpr int length = 20000000;
    constexpr int expected = 16777217;
    chop.start (length, slices, PreviewStretchParams {});
    chop.onNote (60, voices, slices);
    auto& voice = voices.getVoice (VoicePool::kPreviewVoiceIndex);
    if (algorithm == 0)
        voice.position = expected + 0.75;
    else if (algorithm == 1)
    {
        voice.stretchActive = true;
        voice.stretchSrcPos = expected + 0.75;
        voice.stretchOutAvail = 1;
    }
    else
    {
        voice.bungeeActive = true;
        voice.bungeeSrcPos = length + expected + 0.75;
        voice.bungeeOutPosBegin = voice.bungeeSrcPos;
        voice.bungeeOutPosEnd = voice.bungeeSrcPos + 1.0;
        voice.bungeeOutAvail = 1;
    }
    if (algorithm != 0)
    {
        // Consume one already-buffered output sample at the long source phase.
        // No enormous input buffer or stretch-engine seek is needed.
        SampleData emptySample;
        float left = 0.0f, right = 0.0f;
        voices.processVoiceSample (VoicePool::kPreviewVoiceIndex, emptySample,
                                   sampleRate, left, right);
    }
    const int idx = chop.onNote (61, voices, slices);
    require (idx >= 0, "long recording must create a slice");
    require (slices.getSlice (idx).endSample == expected, "chop must retain sample-level precision");
    if (algorithm == 2)
        require (voice.bungeeSrcPos == length + expected + 0.75,
                 "chopping must preserve Bungee's unbounded source phase");
}

constexpr int kOnsetSample = 8000;
constexpr int kBurstStart = 300;
constexpr int kBurstLength = 200;

float onsetContent (int i)
{
    return i >= kOnsetSample ? 0.9f * sineContent (i) : 0.0f;
}

float burstContent (int i)
{
    return (i >= kBurstStart && i < kBurstStart + kBurstLength) ? 0.9f * sineContent (i) : 0.0f;
}

// Stretch engines smear an onset over a few hundred samples, and the 50%
// threshold detects even Repitch a few samples late; allow for both.
constexpr int kAudibleTolerance = 96;

// Bungee's first grain reads the loop tail through the loop mapping, so skip
// the preview start when searching for an onset.
constexpr int kStartSettleFrames = 4096;

void playheadFollowsTheAudibleOnset (int algorithm)
{
    Fixture f (algorithm, false, onsetContent);
    f.chop.onNote (60, f.voices, f.slices);
    std::vector<float> out;
    std::vector<int> heads;
    for (int i = 0; i < 40000; ++i)
    {
        out.push_back (std::abs (f.renderOne()));
        heads.push_back (f.playhead());
    }

    float peak = 0.0f;
    for (size_t i = kStartSettleFrames; i < out.size(); ++i)
        peak = std::max (peak, out[i]);
    require (peak > 0.1f, "onset must be audible");

    int frame = -1;
    for (int i = kStartSettleFrames; i < (int) out.size(); ++i)
    {
        if (out[(size_t) i] > 0.5f * peak)
        {
            frame = i;
            break;
        }
    }
    require (frame >= 0, "onset must be found");
    require (std::abs (heads[(size_t) frame] - kOnsetSample) <= kAudibleTolerance,
             "playhead must match the sample being heard, not the stretcher read head");
}

void loopSeamPlaysEachPassOnce (int algorithm)
{
    Fixture f (algorithm, false, burstContent);
    f.chop.onNote (60, f.voices, f.slices);
    int lastLoud = -100000;
    int previousHead = 0;
    int wraps = 0;
    int bursts = 0;
    for (int i = 0; i < 200000; ++i)
    {
        const float level = std::abs (f.renderOne());
        const int head = f.playhead();
        if (head < previousHead)
            ++wraps;
        previousHead = head;

        if (level > 0.2f)
        {
            if (i - lastLoud > 300)
            {
                ++bursts;
                require (std::abs (head - kBurstStart) <= kAudibleTolerance,
                         "burst must play when the playhead reaches it");
            }
            lastLoud = i;
        }
    }
    require (wraps >= 2, "preview must loop several times");
    require (bursts == wraps + 1, "each loop pass must play the loop start exactly once");
}
} // namespace

int runSampleDataTests (int& passed, int& failed);

int main()
{
    int failed = 0;
    int passed = 0;
    const char* algorithms[] = { "Repitch", "Signalsmith", "Bungee" };
    for (int algorithm = 0; algorithm < 3; ++algorithm)
    {
        const auto run = [&] (const char* name, auto test)
        {
            try
            {
                test (algorithm);
                ++passed;
                std::cout << "PASS " << algorithms[algorithm] << ": " << name << '\n';
            }
            catch (const std::exception& error)
            {
                ++failed;
                std::cerr << "FAIL " << algorithms[algorithm] << ": " << name << ": " << error.what() << '\n';
            }
        };
        run ("chop at advancing playhead", chopsAtTheAdvancingPlayhead);
        run ("loop wrap", closesSlicesAcrossLoopWrap);
        run ("audition before audio render", auditionResetsTheStartWithoutRendering);
        run ("zero-crossing snap", snapsStretchedBoundariesToZeroCrossings);
        run ("long-recording precision", keepsSamplePrecisionForLongRecordings);
        run ("playhead follows audible onset", playheadFollowsTheAudibleOnset);
        run ("loop seam plays each pass once", loopSeamPlaysEachPassOnce);
    }
    runSampleDataTests (passed, failed);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
