#include "VoicePool.h"
#include "../Constants.h"

// Include Signalsmith Stretch
#include "signalsmith-stretch.h"

// Include Bungee
#include "bungee/Bungee.h"

#include <algorithm>
#include <cmath>

static constexpr int kStretchBlockSize = 128;        // required block size for Signalsmith Stretch processing
static constexpr int kMaxStretchInputSamples = 8192; // max pre-roll/input feed size (empirically tuned)
static constexpr int kMaxBungeeInputFrames = 8192;
static constexpr int kMaxBungeeOutputFrames = 8192;

enum class PlaybackDirection
{
    forward = 0,
    reverse,
};

enum class VoiceBoundaryAction
{
    continuePlayback = 0,
    pingPongTurnaround,
    loopWrap,
    releaseTail,
    release,
};

// Forward declaration — defined below, used by initStretcher
static void reseekStretcher (Voice& v, const SampleData& sample, bool consumeSource);

static PlaybackDirection getPlaybackDirection (double step)
{
    return step < 0.0 ? PlaybackDirection::reverse : PlaybackDirection::forward;
}

static void releaseVoiceIfNeeded (Voice& v)
{
    if (v.envelope.getState() != AdsrEnvelope::Release)
    {
        v.envelope.noteOff();
        v.filterEnvelope.noteOff();
    }
}

static VoiceBoundaryAction classifyBoundaryAction (const Voice& v,
                                                   double position,
                                                   PlaybackDirection direction,
                                                   bool reverseBoundaryInclusive)
{
    const bool pastBoundary = direction == PlaybackDirection::forward
        ? (position >= v.endSample)
        : (reverseBoundaryInclusive ? (position <= v.startSample)
                                    : (position < v.startSample));
    if (! pastBoundary)
        return VoiceBoundaryAction::continuePlayback;

    if (v.pingPong)
        return VoiceBoundaryAction::pingPongTurnaround;

    if (v.looping)
        return VoiceBoundaryAction::loopWrap;

    const bool canReleaseTail = direction == PlaybackDirection::forward
        ? (position < v.bufferEnd)
        : (position >= 0.0);
    if (v.releaseTail && canReleaseTail)
        return VoiceBoundaryAction::releaseTail;

    return VoiceBoundaryAction::release;
}

// --- Virtual loop helpers ---

// Wraps pos into the half-open interval [start, end).
// Never duplicates the endpoint. Returns start if span is invalid.
static double wrapLoopPosition (double pos, int start, int end)
{
    const double len = (double) (end - start);
    if (len <= 0.0)
        return (double) start;

    double wrapped = std::fmod (pos - start, len);
    if (wrapped < 0.0)
        wrapped += len;
    return start + wrapped;
}

// Reflects pos through a ping-pong cycle over [start, end-1].
// Returns the reflected position only (stateless position mapping).
// Does not duplicate the turnaround sample.
static double reflectPingPongPosition (double pos, int start, int end)
{
    const double lo = (double) start;
    const double hi = (double) (end - 1);
    const double span = hi - lo;
    if (span <= 0.0)
        return lo;

    double t = pos - lo;
    if (t < 0.0)
        t = -t;

    const double fullCycle = 2.0 * span;
    t = std::fmod (t, fullCycle);
    if (t < 0.0)
        t += fullCycle;

    if (t <= span)
        return lo + t;
    else
        return lo + (fullCycle - t);
}

static float readClampedSample (const SampleData& sample, double pos, int channel)
{
    const int maxFrame = sample.getNumFrames() - 1;
    if (maxFrame < 0)
        return 0.0f;

    return sample.getInterpolatedSample (juce::jlimit (0.0, (double) maxFrame, pos), channel);
}

static float readBoundedSliceSample (const SampleData& sample, double pos,
                                     int channel, int start, int end)
{
    const int sliceLen = end - start;
    if (sliceLen <= 0)
        return 0.0f;

    const auto& buffer = sample.getBuffer();
    if (channel < 0 || channel >= buffer.getNumChannels())
        return 0.0f;

    const auto* data = buffer.getReadPointer (channel);
    if (data == nullptr)
        return 0.0f;

    const double clampedPos = juce::jlimit ((double) start,
                                            (double) juce::jmax (start, end - 1),
                                            pos);
    const int frame0 = juce::jlimit (start, end - 1, (int) std::floor (clampedPos));
    const float frac = (float) (clampedPos - std::floor (clampedPos));
    const int frame1 = juce::jlimit (start, end - 1, frame0 + 1);

    return data[frame0] + (data[frame1] - data[frame0]) * frac;
}

static float readWrappedLoopSample (const SampleData& sample, double pos,
                                    int channel, int start, int end)
{
    const int loopLen = end - start;
    if (loopLen <= 0)
        return 0.0f;

    const auto& buffer = sample.getBuffer();
    if (channel < 0 || channel >= buffer.getNumChannels())
        return 0.0f;

    const auto* data = buffer.getReadPointer (channel);
    if (data == nullptr)
        return 0.0f;

    const double wrappedPos = wrapLoopPosition (pos, start, end);
    const int frame0 = juce::jlimit (start, end - 1, (int) std::floor (wrappedPos));
    const float frac = (float) (wrappedPos - std::floor (wrappedPos));
    const int frame1 = (frame0 + 1 < end) ? (frame0 + 1) : start;

    return data[frame0] + (data[frame1] - data[frame0]) * frac;
}

static RepitchMode getActiveRepitchMode (int storedMode)
{
    return storedMode == (int) RepitchMode::Cubic ? RepitchMode::Cubic
                                                  : RepitchMode::Linear;
}

template <typename FrameMapper>
static float readMappedRepitchSample (const SampleData& sample, double pos, int channel,
                                      RepitchMode mode, FrameMapper&& mapFrame)
{
    const double baseFloor = std::floor (pos);
    const int base = (int) baseFloor;
    const float frac = (float) (pos - baseFloor);

    const auto sampleAt = [&] (int frame)
    {
        return sample.getSampleAtFrame (mapFrame (frame), channel);
    };

    if (mode == RepitchMode::Cubic)
    {
        return SampleData::interpolateCubic (sampleAt (base - 1),
                                             sampleAt (base),
                                             sampleAt (base + 1),
                                             sampleAt (base + 2),
                                             frac);
    }

    const float s0 = sampleAt (base);
    const float s1 = sampleAt (base + 1);
    return s0 + (s1 - s0) * frac;
}

static int wrapLoopFrame (int frame, int start, int end)
{
    const int len = end - start;
    if (len <= 0)
        return start;

    int wrapped = (frame - start) % len;
    if (wrapped < 0)
        wrapped += len;
    return start + wrapped;
}

static int reflectPingPongFrame (int frame, int start, int end)
{
    return juce::roundToInt (reflectPingPongPosition ((double) frame, start, end));
}

static float readRepitchClampedSample (const SampleData& sample, double pos,
                                       int channel, RepitchMode mode)
{
    const int maxFrame = sample.getNumFrames() - 1;
    if (maxFrame < 0)
        return 0.0f;

    return readMappedRepitchSample (sample, pos, channel, mode,
                                    [maxFrame] (int frame)
                                    {
                                        return juce::jlimit (0, maxFrame, frame);
                                    });
}

static float readRepitchBoundedSliceSample (const SampleData& sample, double pos,
                                            int channel, int start, int end,
                                            RepitchMode mode)
{
    const int sliceLen = end - start;
    if (sliceLen <= 0)
        return 0.0f;

    return readMappedRepitchSample (sample, pos, channel, mode,
                                    [start, end] (int frame)
                                    {
                                        return juce::jlimit (start, end - 1, frame);
                                    });
}

static float readRepitchWrappedLoopSample (const SampleData& sample, double pos,
                                           int channel, int start, int end,
                                           RepitchMode mode)
{
    const int loopLen = end - start;
    if (loopLen <= 0)
        return 0.0f;

    return readMappedRepitchSample (sample, pos, channel, mode,
                                    [start, end] (int frame)
                                    {
                                        return wrapLoopFrame (frame, start, end);
                                    });
}

// Reads a sample from the virtual looped source at an arbitrary position.
// For loop/ping-pong modes, maps the position through the virtual loop.
// For non-loop modes, clamps to buffer bounds.
static float readExactLoopSample (const Voice& v, const SampleData& sample,
                                  double pos, int channel)
{
    if (v.pingPong)
    {
        double mapped = reflectPingPongPosition (pos, v.startSample, v.endSample);
        return readClampedSample (sample, mapped, channel);
    }

    if (v.looping)
    {
        return readWrappedLoopSample (sample, pos, channel, v.startSample, v.endSample);
    }

    return readClampedSample (sample, pos, channel);
}

static float readRepitchExactLoopSample (const Voice& v, const SampleData& sample,
                                         double pos, int channel)
{
    const auto mode = getActiveRepitchMode (v.repitchMode);

    if (v.pingPong)
    {
        return readMappedRepitchSample (sample, pos, channel, mode,
                                        [&v] (int frame)
                                        {
                                            return reflectPingPongFrame (frame, v.startSample, v.endSample);
                                        });
    }

    if (v.looping)
        return readRepitchWrappedLoopSample (sample, pos, channel, v.startSample, v.endSample, mode);

    return readRepitchClampedSample (sample, pos, channel, mode);
}

// --- Crossfade helpers ---

// Equal-power crossfade gains using a fast cosine approximation.
// t in [0,1]: 0 = full main, 1 = full crossfade source.
// Uses the identity: gain_main = cos(t * pi/2), gain_xfade = sin(t * pi/2)
// Approximated with a polynomial: cos(x) ≈ 1 - x²/2 + x⁴/24 (good to ~0.01% error)
static inline void equalPowerGains (float t, float& gainMain, float& gainXfade)
{
    const float halfPi = juce::MathConstants<float>::halfPi;
    const float a = t * halfPi;
    const float b = (1.0f - t) * halfPi;
    // Use std::cos for correctness; the compiler will typically vectorize this
    gainMain  = std::cos (a);
    gainXfade = std::cos (b);
}

static double mapCrossfadePosition (const Voice& v, double pos)
{
    if (v.pingPong)
        return reflectPingPongPosition (pos, v.startSample, v.endSample);

    if (v.looping)
        return wrapLoopPosition (pos, v.startSample, v.endSample);

    return pos;
}

// Returns the distance (in rendered samples) from the current position to the
// approaching seam. Looping modes use canonical wrapped/reflected positions so
// long-running phases (notably Bungee) do not get stuck in the fade zone after
// the first wrap.
static inline int distanceToBoundary (const Voice& v, double pos, int direction)
{
    const double mappedPos = mapCrossfadePosition (v, pos);

    if (direction >= 0)
    {
        const double boundary = (double) juce::jmax (v.startSample, v.endSample - 1);
        const double dist = boundary - mappedPos;
        return (dist > 0.0) ? (int) std::ceil (dist) : 0;
    }

    const double dist = mappedPos - (double) v.startSample;
    return (dist > 0.0) ? (int) std::ceil (dist) : 0;
}

static double getCrossfadeSourcePos (const Voice& v, int dist, int direction)
{
    if (v.pingPong)
    {
        if (direction >= 0)
            return (double) v.endSample + (double) dist;

        return (double) v.startSample - (double) dist;
    }

    // Normal loop: use dist so source advances in playback direction,
    // converging to the wrap-destination at the boundary (dist=0)
    if (direction >= 0)
        return (double) v.startSample - (double) dist;

    return (double) v.endSample + (double) dist;
}

static float readCrossfadeMainSample (const Voice& v, const SampleData& sample,
                                      double pos, int channel)
{
    if (v.looping || v.pingPong)
        return readBoundedSliceSample (sample, mapCrossfadePosition (v, pos),
                                       channel, v.startSample, v.endSample);

    return readExactLoopSample (v, sample, pos, channel);
}

static float readRepitchCrossfadeMainSample (const Voice& v, const SampleData& sample,
                                             double pos, int channel)
{
    const auto mode = getActiveRepitchMode (v.repitchMode);

    if (v.looping || v.pingPong)
    {
        return readRepitchBoundedSliceSample (sample, mapCrossfadePosition (v, pos),
                                              channel, v.startSample, v.endSample, mode);
    }

    return readRepitchExactLoopSample (v, sample, pos, channel);
}

// Reads a crossfaded sample at the given position. The secondary source depends on
// the seam type: normal loops read outside the slice, ping-pong reads the
// boundary-adjacent region outside the slice in the opposite travel direction.
static float readCrossfadedSample (const Voice& v, const SampleData& sample,
                                   double pos, int channel, int direction)
{
    if (v.crossfadeLenSamples <= 0)
        return readExactLoopSample (v, sample, pos, channel);

    const int dist = distanceToBoundary (v, pos, direction);
    if (dist >= v.crossfadeLenSamples)
        return readExactLoopSample (v, sample, pos, channel);

    // We're in the crossfade zone
    const float t = 1.0f - (float) dist / (float) v.crossfadeLenSamples;
    float gainMain, gainXfade;
    equalPowerGains (t, gainMain, gainXfade);

    const float mainSample = readCrossfadeMainSample (v, sample, pos, channel);
    const float xfadeSample = readClampedSample (sample, getCrossfadeSourcePos (v, dist, direction), channel);

    return gainMain * mainSample + gainXfade * xfadeSample;
}

static float readRepitchCrossfadedSample (const Voice& v, const SampleData& sample,
                                          double pos, int channel, int direction)
{
    if (v.crossfadeLenSamples <= 0)
        return readRepitchExactLoopSample (v, sample, pos, channel);

    const int dist = distanceToBoundary (v, pos, direction);
    if (dist >= v.crossfadeLenSamples)
        return readRepitchExactLoopSample (v, sample, pos, channel);

    const float t = 1.0f - (float) dist / (float) v.crossfadeLenSamples;
    float gainMain, gainXfade;
    equalPowerGains (t, gainMain, gainXfade);

    const auto mode = getActiveRepitchMode (v.repitchMode);
    const float mainSample = readRepitchCrossfadeMainSample (v, sample, pos, channel);
    const float xfadeSample = readRepitchClampedSample (sample, getCrossfadeSourcePos (v, dist, direction),
                                                        channel, mode);

    return gainMain * mainSample + gainXfade * xfadeSample;
}

// Computes the crossfade source position for UI cursor display.
// Returns 0 when the voice is not in the crossfade zone.
static float computeXfadeSourceForUI (const Voice& v, double pos, PlaybackDirection dir)
{
    if (v.crossfadeLenSamples <= 0) return 0.0f;
    const int direction = (dir == PlaybackDirection::forward) ? 1 : -1;
    const int dist = distanceToBoundary (v, pos, direction);
    if (dist >= v.crossfadeLenSamples) return 0.0f;
    return (float) getCrossfadeSourcePos (v, dist, direction);
}

// Advances v.stretchSrcPos by v.direction and handles loop/ping-pong wrapping.
// Returns the boundary action so the caller can still handle release cases.
static VoiceBoundaryAction advanceStretchSrcPos (Voice& v)
{
    v.stretchSrcPos += (double) v.direction;

    if (v.pingPong)
    {
        if (v.stretchSrcPos >= v.endSample)
        {
            v.stretchSrcPos = 2.0 * (v.endSample - 1) - v.stretchSrcPos;
            v.direction = -1;
            return VoiceBoundaryAction::pingPongTurnaround;
        }
        else if (v.stretchSrcPos < v.startSample)
        {
            v.stretchSrcPos = 2.0 * v.startSample - v.stretchSrcPos;
            v.direction = 1;
            return VoiceBoundaryAction::pingPongTurnaround;
        }
        return VoiceBoundaryAction::continuePlayback;
    }

    if (v.looping)
    {
        const bool wrapped = v.stretchSrcPos >= v.endSample || v.stretchSrcPos < v.startSample;
        v.stretchSrcPos = wrapLoopPosition (v.stretchSrcPos, v.startSample, v.endSample);
        return wrapped ? VoiceBoundaryAction::loopWrap
                       : VoiceBoundaryAction::continuePlayback;
    }

    // Non-loop: classify boundary for release/releaseTail
    return classifyBoundaryAction (v, v.stretchSrcPos, getPlaybackDirection ((double) v.direction), true);
}

static inline float dbToLinear (float dB)
{
    if (dB <= -100.0f) return 0.0f;
    return std::pow (10.0f, dB / 20.0f);
}

static inline void cacheSaturationConstants (Voice& v)
{
    if (v.filterDrive <= 0.0f)
    {
        v.filterDriveGain = 0.0f;
        v.satBias = v.satOffset = 0.0f;
        v.satNorm = 1.0f;
        v.dcBlockerActive = false;
        return;
    }
    const float driveNorm = juce::jlimit (0.0f, 1.0f, v.filterDrive / 100.0f);
    const float asymNorm  = juce::jlimit (0.0f, 1.0f, v.filterAsym / 100.0f);
    const float gain = 1.0f + driveNorm * 12.0f;
    const float bias = asymNorm * 0.5f;

    v.filterDriveGain = gain;
    v.satBias   = bias;
    v.satOffset = std::tanh (bias * gain);
    const float pos = std::abs (std::tanh ((1.0f + bias) * gain) - v.satOffset);
    const float neg = std::abs (std::tanh ((-1.0f + bias) * gain) - v.satOffset);
    v.satNorm   = juce::jmax (1e-6f, juce::jmax (pos, neg));
    v.dcBlockerActive = asymNorm > 0.0f;
}

static inline float saturateSample (float x, const Voice& v)
{
    if (v.filterDriveGain <= 0.0f)
        return x;
    return (std::tanh ((x + v.satBias) * v.filterDriveGain) - v.satOffset) / v.satNorm;
}

static inline float resonancePercentToQ (float resonancePercent)
{
    const float norm = juce::jlimit (0.0f, 1.0f, resonancePercent / 100.0f);
    constexpr float kMinQ = 0.7071f;
    constexpr float kMaxQ = 12.0f;
    return kMinQ * std::pow (kMaxQ / kMinQ, norm);
}

static inline float computeFilterCutoff (const Voice& v, float sampleRate)
{
    const float keyTrackedCutoff = v.filterCutoff * v.filterKeyTrackRatio;
    const float envLevel = v.filterEnvelope.getLevel();
    const float cutoff = keyTrackedCutoff * std::exp2f (v.filterEnvAmount * envLevel / 12.0f);
    return juce::jlimit (kMinFilterCutoffHz, sampleRate * 0.49f, cutoff);
}

static constexpr int kFilterCoeffUpdateInterval = 32;

static inline void processVoiceFilter (Voice& v, float sampleRate, float& inOutL, float& inOutR)
{
    if (! v.filterEnabled)
        return;

    if (--v.filterCoeffCounter <= 0)
    {
        v.filterCoeffCounter = kFilterCoeffUpdateInterval;
        const float q = resonancePercentToQ (v.filterReso);
        const float cutoff = computeFilterCutoff (v, sampleRate);
        v.filterCoeffs = SvfFilter::computeCoeffs (cutoff, q, sampleRate);
    }

    inOutL = saturateSample (inOutL, v);
    inOutR = saturateSample (inOutR, v);

    if (v.dcBlockerActive)
    {
        float outL = inOutL - v.dcPrevInL + v.dcCoeffR * v.dcPrevOutL;
        v.dcPrevInL = inOutL;  v.dcPrevOutL = outL;  inOutL = outL;

        float outR = inOutR - v.dcPrevInR + v.dcCoeffR * v.dcPrevOutR;
        v.dcPrevInR = inOutR;  v.dcPrevOutR = outR;  inOutR = outR;
    }

    float yL = v.filterL1.process (inOutL, v.filterCoeffs, v.filterType);
    float yR = v.filterR1.process (inOutR, v.filterCoeffs, v.filterType);

    if (v.filterSlope > 0)
    {
        yL = v.filterL2.process (yL, v.filterCoeffs, v.filterType);
        yR = v.filterR2.process (yR, v.filterCoeffs, v.filterType);
    }

    inOutL = yL;
    inOutR = yR;
}

VoicePool::VoicePool()
{
    for (auto& p : voicePositions)
        p.store (0.0f, std::memory_order_relaxed);
    for (auto& p : xfadeSourcePositions)
        p.store (0.0f, std::memory_order_relaxed);

    for (auto& v : voices)
    {
        v.stretchInBufL.resize (kMaxStretchInputSamples);
        v.stretchInBufR.resize (kMaxStretchInputSamples);
        v.stretchOutBufL.resize (kStretchBlockSize);
        v.stretchOutBufR.resize (kStretchBlockSize);
        v.bungeeInputBuf.resize ((size_t) kMaxBungeeInputFrames * 2);
        v.bungeeOutBufL.resize (kMaxBungeeOutputFrames);
        v.bungeeOutBufR.resize (kMaxBungeeOutputFrames);
    }
}

void VoicePool::prepareToPlay (double sr, int maxBlockSize)
{
    setSampleRate (sr);
    const int scratchSize = juce::jmax (1, maxBlockSize);
    scratchL.resize ((size_t) scratchSize, 0.0f);
    scratchR.resize ((size_t) scratchSize, 0.0f);
}

void VoicePool::setSampleRate (double sr)
{
    sampleRate = sr;
}

int VoicePool::allocate()
{
    // First pass: find inactive voice within maxActive range
    auto it = std::find_if (voices.begin(), voices.begin() + maxActive,
                            [] (const Voice& v) { return ! v.active; });
    if (it != voices.begin() + maxActive)
        return (int) std::distance (voices.begin(), it);

    // Second pass: steal — prefer releasing voices with lowest envelope
    int best = 0;
    float bestScore = 999999.0f;

    for (int i = 0; i < maxActive; ++i)
    {
        float score = voices[i].envelope.getLevel();
        if (voices[i].envelope.getState() == AdsrEnvelope::Release)
            score -= 10.0f;
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }

    return best;
}

void VoicePool::setMaxActiveVoices (int n)
{
    n = juce::jlimit (1, kMaxVoices - 1, n);
    if (n < maxActive)
    {
        // Kill voices beyond new limit (preview is permanently reserved).
        constexpr int previewIdx = kPreviewVoiceIndex;
        for (int i = n; i < maxActive; ++i)
        {
            if (i == previewIdx) continue;
            voices[i].active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            xfadeSourcePositions[i].store (0.0f, std::memory_order_relaxed);
        }
    }
    maxActive = n;
}

void VoicePool::initPreviewVoiceCommon (Voice& v,
                                        int playheadSample,
                                        int startSample,
                                        int endSample,
                                        bool looping,
                                        float velocity)
{
    v.active        = true;
    v.sliceIdx      = -1;
    v.position      = (double) playheadSample;
    v.speed         = 1.0;
    v.direction     = 1;
    v.midiNote      = -1;
    v.repitchMode   = (int) RepitchMode::Linear;
    v.velocity      = velocity;
    v.startSample   = startSample;
    v.endSample     = endSample;
    v.bufferEnd     = endSample;
    v.pingPong      = false;
    v.muteGroup     = 0;
    v.looping       = looping;
    v.volume        = 1.0f;
    v.releaseTail   = false;
    v.oneShot       = false;
    v.filterEnabled = false;
    v.filterCutoff  = 8200.0f;
    v.filterReso    = 0.0f;
    v.filterDrive   = 0.0f;
    v.filterAsym    = 0.0f;
    v.filterDriveGain = 0.0f;
    v.satBias = v.satOffset = 0.0f;
    v.satNorm = 1.0f;
    v.crossfadePct = 0.0f;
    v.crossfadeLenSamples = 0;
    v.dcBlockerActive = false;
    v.dcPrevInL = v.dcPrevInR = v.dcPrevOutL = v.dcPrevOutR = 0.0f;
    v.dcCoeffR = 0.0f;
    v.filterEnvAmount = 0.0f;
    v.filterKeyTrackRatio = 1.0f;
    v.filterL1.reset();
    v.filterR1.reset();
    v.filterL2.reset();
    v.filterR2.reset();
    v.stretchActive = false;
    v.stretchOutReadPos = 0;
    v.stretchOutAvail   = 0;
    v.stretchResetNeeded = false;
    v.bungeeActive  = false;
    v.bungeeOutReadPos = 0;
    v.bungeeOutAvail   = 0;
    v.bungeeResetNeeded = false;
}

void VoicePool::initPreviewVoiceStretch (Voice& v, int sourceStartSample, const PreviewStretchParams& p)
{
    v.repitchMode = p.repitchMode;

    if (p.stretchEnabled && p.dawBpm > 0.0f && p.bpm > 0.0f)
    {
        const float speedRatio = p.dawBpm / p.bpm;
        if (p.algorithm == 0)
        {
            v.speed = speedRatio;
            return;
        }

        if (p.algorithm == 2 && p.sample != nullptr)
        {
            v.bungeeActive = true;
            v.bungeeSpeed = (double) speedRatio;
            v.bungeeSrcPos = sourceStartSample;
            initBungee (v, p.pitch, p.sampleRate, juce::jlimit (-1, 1, p.grainMode - 1));
            return;
        }

        if (p.sample != nullptr)
        {
            v.stretchActive = true;
            v.stretchTimeRatio = speedRatio;
            v.stretchPitchSemis = p.pitch;
            v.stretchSrcPos = sourceStartSample;
            initStretcher (v, p.pitch, p.sampleRate,
                           p.tonality, p.formant, p.formantComp, *p.sample);
            return;
        }
    }

    if (p.algorithm == 1 && p.sample != nullptr)
    {
        v.stretchActive = true;
        v.stretchTimeRatio = 1.0f;
        v.stretchPitchSemis = p.pitch;
        v.stretchSrcPos = sourceStartSample;
        initStretcher (v, p.pitch, p.sampleRate,
                       p.tonality, p.formant, p.formantComp, *p.sample);
        return;
    }

    if (p.algorithm == 2 && p.sample != nullptr)
    {
        v.bungeeActive = true;
        v.bungeeSpeed = 1.0;
        v.bungeeSrcPos = sourceStartSample;
        initBungee (v, p.pitch, p.sampleRate, juce::jlimit (-1, 1, p.grainMode - 1));
        return;
    }

    v.speed = std::pow (2.0f, p.pitch / 12.0f);
}

void VoicePool::initStretcher (Voice& v, float pitchSemis, double sr,
                               float tonalityHz, float formantSemis, bool formantComp,
                               const SampleData& sample)
{
    int blockSize = std::max (256, (int)(sr * 0.023));   // ~1024 @ 44.1k (~23ms)
    int interval  = std::max (64,  (int)(sr * 0.006));   // ~256 @ 44.1k (~6ms)

    if (! v.stretcher)
        v.stretcher = std::make_shared<signalsmith::stretch::SignalsmithStretch<float, void>>();
    v.stretcher->configure (2, blockSize, interval, false);

    float tonalityLimit = (tonalityHz > 0.0f && sr > 0.0) ? (float)(tonalityHz / sr) : 0.0f;
    v.stretcher->setTransposeSemitones (pitchSemis, tonalityLimit);

    if (formantSemis != 0.0f || formantComp)
        v.stretcher->setFormantSemitones (formantSemis, formantComp);

    v.stretchOutReadPos = 0;
    v.stretchOutAvail = 0;

    // Pre-roll from current stretchSrcPos via shared reseek helper
    reseekStretcher (v, sample, true);
}

void VoicePool::initBungee (Voice& v, float pitchSemis, double sr, int grainMode)
{
    Bungee::SampleRates rates;
    rates.input  = (int) sr;
    rates.output = (int) sr;

    int hopAdj = juce::jlimit (-1, 1, grainMode);
    v.bungeeStretcher = std::make_shared<Bungee::Stretcher<Bungee::Basic>> (rates, 2, hopAdj);

    v.bungeePitch = std::pow (2.0, (double) pitchSemis / 12.0);
    v.bungeeOutReadPos = 0;
    v.bungeeOutAvail = 0;
}

void VoicePool::startVoice (int voiceIdx, const VoiceStartParams& p,
                            SliceManager& sm, const SampleData& sample)
{
    auto& v = voices[voiceIdx];
    const int sliceIdx = p.sliceIdx;
    const auto& s = sm.getSlice (sliceIdx);

    v.active    = true;
    v.sliceIdx  = sliceIdx;
    v.midiNote  = p.note;
    v.velocity  = p.velocity / 127.0f;

    v.startSample = s.startSample;
    v.endSample   = s.endSample;

    // Resolve parameters via inheritance
    float attack   = sm.resolveParam (sliceIdx, kLockAttack,   s.attackSec,    p.globalAttackSec);
    float decay    = sm.resolveParam (sliceIdx, kLockDecay,    s.decaySec,     p.globalDecaySec);
    float sustain  = sm.resolveParam (sliceIdx, kLockSustain,  s.sustainLevel, p.globalSustain);
    float release  = sm.resolveParam (sliceIdx, kLockRelease,  s.releaseSec,   p.globalReleaseSec);

    v.envelope.noteOn (attack, decay, sustain, release, sampleRate);

    int resolvedLoopMode = (int) sm.resolveParam (sliceIdx, kLockLoop, (float) s.loopMode, (float) p.globalLoopMode);
    v.looping    = (resolvedLoopMode == 1);
    v.pingPong   = (resolvedLoopMode == 2);
    v.muteGroup  = (int) sm.resolveParam (sliceIdx, kLockMuteGroup, (float) s.muteGroup, (float) p.globalMuteGroup);

    bool rev = sm.resolveParam (sliceIdx, kLockReverse,
                                 s.reverse ? 1.0f : 0.0f,
                                 p.globalReverse ? 1.0f : 0.0f) > 0.5f;
    v.direction = rev ? -1 : 1;
    v.position  = rev ? (s.endSample - 1) : s.startSample;

    v.outputBus = (int) sm.resolveParam (sliceIdx, kLockOutputBus, (float) s.outputBus, 0.0f);

    int algo = (int) sm.resolveParam (sliceIdx, kLockAlgorithm, (float) s.algorithm, (float) p.globalAlgorithm);
    int repitchMode = (int) sm.resolveParam (sliceIdx, kLockRepitchMode,
                                             (float) s.repitchMode, (float) p.globalRepitchMode);

    float sliceBpm = sm.resolveParam (sliceIdx, kLockBpm,   s.bpm,            p.globalBpm);
    float pitchSt  = sm.resolveParam (sliceIdx, kLockPitch,       s.pitchSemitones, p.globalPitch);
    float cents    = sm.resolveParam (sliceIdx, kLockCentsDetune, s.centsDetune,    p.globalCentsDetune);
    float pitch    = pitchSt + cents / 100.0f;
    float pitchRatio = std::pow (2.0f, pitch / 12.0f);

    bool stretchOn = sm.resolveParam (sliceIdx, kLockStretch,
                                       s.stretchEnabled ? 1.0f : 0.0f,
                                       p.globalStretch ? 1.0f : 0.0f) > 0.5f;

    float tonality = sm.resolveParam (sliceIdx, kLockTonality,    s.tonalityHz,       p.globalTonality);
    float formant  = sm.resolveParam (sliceIdx, kLockFormant,     s.formantSemitones, p.globalFormant);
    bool fComp     = sm.resolveParam (sliceIdx, kLockFormantComp,
                                       s.formantComp ? 1.0f : 0.0f,
                                       p.globalFormantComp ? 1.0f : 0.0f) > 0.5f;

    int grainMode = (int) sm.resolveParam (sliceIdx, kLockGrainMode,
                                           (float) s.grainMode, (float) p.globalGrainMode);
    // Convert grainMode index (0=Fast, 1=Normal, 2=Smooth) to log2 hop adjust (-1, 0, +1)
    int hopAdj = grainMode - 1;

    v.volume = dbToLinear (sm.resolveParam (sliceIdx, kLockVolume, s.volume, p.globalVolume));
    v.repitchMode = juce::jlimit (0, 2, repitchMode);

    v.releaseTail = sm.resolveParam (sliceIdx, kLockReleaseTail,
                                      s.releaseTail ? 1.0f : 0.0f,
                                      p.globalReleaseTail ? 1.0f : 0.0f) > 0.5f;
    v.oneShot = sm.resolveParam (sliceIdx, kLockOneShot,
                                  s.oneShot ? 1.0f : 0.0f,
                                  p.globalOneShot ? 1.0f : 0.0f) > 0.5f;
    v.filterEnabled = sm.resolveParam (sliceIdx, kLockFilterEnabled,
                                       s.filterEnabled ? 1.0f : 0.0f,
                                       p.globalFilterEnabled ? 1.0f : 0.0f) > 0.5f;
    v.filterType = (int) sm.resolveParam (sliceIdx, kLockFilterType,
                                          (float) s.filterType, (float) p.globalFilterType);
    v.filterSlope = (int) sm.resolveParam (sliceIdx, kLockFilterSlope,
                                           (float) s.filterSlope, (float) p.globalFilterSlope);
    v.filterCutoff = sm.resolveParam (sliceIdx, kLockFilterCutoff,
                                      s.filterCutoff, p.globalFilterCutoff);
    v.filterReso = sm.resolveParam (sliceIdx, kLockFilterReso,
                                    s.filterReso, p.globalFilterReso);
    v.filterDrive = sm.resolveParam (sliceIdx, kLockFilterDrive,
                                     s.filterDrive, p.globalFilterDrive);
    v.filterAsym = sm.resolveParam (sliceIdx, kLockFilterAsym,
                                    s.filterAsym, p.globalFilterAsym);
    cacheSaturationConstants (v);
    v.dcCoeffR = 1.0f - (2.0f * juce::MathConstants<float>::pi * 20.0f / (float) sampleRate);
    v.dcPrevInL = v.dcPrevInR = v.dcPrevOutL = v.dcPrevOutR = 0.0f;
    const float keyTrackPercent = sm.resolveParam (sliceIdx, kLockFilterKeyTrack,
                                                   s.filterKeyTrack, p.globalFilterKeyTrack);
    const float keyTrackNorm = juce::jlimit (0.0f, 1.0f, keyTrackPercent / 100.0f);
    const float noteRatio = std::pow (2.0f, ((float) p.note - (float) p.rootNote) / 12.0f);
    v.filterKeyTrackRatio = std::pow (noteRatio, keyTrackNorm);
    const float filterEnvAttack = sm.resolveParam (sliceIdx, kLockFilterEnvAttack,
                                                   s.filterEnvAttackSec, p.globalFilterEnvAttackSec);
    const float filterEnvDecay = sm.resolveParam (sliceIdx, kLockFilterEnvDecay,
                                                  s.filterEnvDecaySec, p.globalFilterEnvDecaySec);
    const float filterEnvSustain = sm.resolveParam (sliceIdx, kLockFilterEnvSustain,
                                                    s.filterEnvSustain, p.globalFilterEnvSustain);
    const float filterEnvRelease = sm.resolveParam (sliceIdx, kLockFilterEnvRelease,
                                                    s.filterEnvReleaseSec, p.globalFilterEnvReleaseSec);
    v.filterEnvAmount = sm.resolveParam (sliceIdx, kLockFilterEnvAmount,
                                         s.filterEnvAmount, p.globalFilterEnvAmount);
    v.bufferEnd = sample.getNumFrames();

    // Resolve crossfade
    v.crossfadePct = sm.resolveParam (sliceIdx, kLockCrossfade, s.crossfadePct, p.globalCrossfadePct);
    {
        const int sliceLen = s.endSample - s.startSample;
        if (v.crossfadePct > 0.0f && sliceLen > 0 && (v.looping || v.pingPong))
        {
            int fadeLen = crossfadePercentToSamples (v.crossfadePct, sliceLen, v.pingPong);

            if (v.pingPong)
                fadeLen = clampPingPongCrossfadeLengthSamples (fadeLen, s.startSample, s.endSample, v.bufferEnd);
            else if (v.looping)
                fadeLen = clampLoopCrossfadeLengthSamples (fadeLen, s.startSample, s.endSample, v.bufferEnd, rev);

            v.crossfadeLenSamples = juce::jmax (0, fadeLen);
        }
        else
        {
            v.crossfadeLenSamples = 0;
        }
    }

    v.filterL1.reset();
    v.filterR1.reset();
    v.filterL2.reset();
    v.filterR2.reset();
    v.filterCoeffCounter = 0;
    v.filterEnvelope.noteOn (filterEnvAttack, filterEnvDecay, filterEnvSustain, filterEnvRelease, sampleRate);

    // Reset stretch state (guard against stale data from stolen voices)
    v.stretchActive  = false;
    v.stretchResetNeeded = false;
    v.bungeeActive   = false;
    v.bungeeResetNeeded = false;

    if (stretchOn && p.dawBpm > 0.0f && sliceBpm > 0.0f)
    {
        float speedRatio = p.dawBpm / sliceBpm;

        if (algo == 0)
        {
            // Repitch: BPM ratio drives speed (pitch is a consequence of speed)
            v.speed = speedRatio;
        }
        else if (algo == 2)
        {
            // Bungee: independent pitch + time
            v.bungeeActive = true;
            v.speed = 1.0;
            v.bungeeSpeed = rev ? -(double) speedRatio : (double) speedRatio;
            v.bungeeSrcPos = rev ? (s.endSample - 1) : s.startSample;

            initBungee (v, pitch, sampleRate, hopAdj);
        }
        else
        {
            // Signalsmith Stretch: independent pitch + time
            v.stretchActive = true;
            v.speed = 1.0;
            v.stretchTimeRatio = speedRatio;
            v.stretchPitchSemis = pitch;
            v.stretchSrcPos = rev ? (s.endSample - 1) : s.startSample;

            initStretcher (v, pitch, sampleRate, tonality, formant, fComp, sample);
        }
    }
    else
    {
        if (algo == 1)
        {
            // Stretch algo but no stretch enabled — use Signalsmith for pitch only
            v.stretchActive = true;
            v.speed = 1.0;
            v.stretchTimeRatio = 1.0f;
            v.stretchPitchSemis = pitch;
            v.stretchSrcPos = rev ? (s.endSample - 1) : s.startSample;

            initStretcher (v, pitch, sampleRate, tonality, formant, fComp, sample);
        }
        else if (algo == 2)
        {
            // Bungee algo but no stretch — use Bungee for pitch only
            v.bungeeActive = true;
            v.speed = 1.0;
            v.bungeeSpeed = rev ? -1.0 : 1.0;
            v.bungeeSrcPos = rev ? (s.endSample - 1) : s.startSample;

            initBungee (v, pitch, sampleRate, hopAdj);
        }
        else
        {
            // Repitch: direct playback
            v.speed = pitchRatio;
        }
    }
}

void VoicePool::releaseNote (int note)
{
    for (int i = 0; i < maxActive; ++i)
    {
        if (voices[i].active && voices[i].midiNote == note)
        {
            if (voices[i].oneShot)
                continue;   // ignore note-off; voice plays through to endSample
            voices[i].envelope.noteOff();
            voices[i].filterEnvelope.noteOff();
        }
    }
}

void VoicePool::releaseNoteForced (int note)
{
    for (int i = 0; i < maxActive; ++i)
        if (voices[i].active && voices[i].midiNote == note)
        {
            voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
            voices[i].filterEnvelope.forceRelease (kKillReleaseSec, sampleRate);
        }
}

void VoicePool::releaseAll()
{
    for (int i = 0; i < maxActive; ++i)
        if (voices[i].active)
        {
            voices[i].envelope.forceRelease (kShortReleaseSec, sampleRate);
            voices[i].filterEnvelope.forceRelease (kShortReleaseSec, sampleRate);
        }
}

void VoicePool::killAll()
{
    for (int i = 0; i < maxActive; ++i)
        if (voices[i].active)
        {
            voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
            voices[i].filterEnvelope.forceRelease (kKillReleaseSec, sampleRate);
        }
}

void VoicePool::muteGroup (int group, int exceptVoice)
{
    if (group <= 0)
        return;

    for (int i = 0; i < maxActive; ++i)
    {
        if (i != exceptVoice && voices[i].active && voices[i].muteGroup == group)
        {
            voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
            voices[i].filterEnvelope.forceRelease (kKillReleaseSec, sampleRate);
        }
    }
}

// Reseek the Signalsmith stretcher from the current stretchSrcPos/direction.
// Extracted from initStretcher() so it can also be called at loop/ping-pong seams.
static void reseekStretcher (Voice& v, const SampleData& sample, bool consumeSource)
{
    if (! v.stretcher || ! sample.isLoaded())
        return;

    float playbackRate = v.stretchTimeRatio;
    int seekLen = v.stretcher->outputSeekLength (playbackRate);
    seekLen = std::min (seekLen, v.endSample - v.startSample);
    seekLen = juce::jlimit (0, (int) v.stretchInBufL.size(), seekLen);

    if (seekLen > 0 && sample.getNumFrames() > 0)
    {
        for (int i = 0; i < seekLen; ++i)
        {
            double srcPos = (v.direction > 0)
                ? v.stretchSrcPos + i
                : v.stretchSrcPos - i;
            v.stretchInBufL[(size_t) i] = readExactLoopSample (v, sample, srcPos, 0);
            v.stretchInBufR[(size_t) i] = readExactLoopSample (v, sample, srcPos, 1);
        }
        float* ptrs[2] = { v.stretchInBufL.data(), v.stretchInBufR.data() };
        v.stretcher->outputSeek (ptrs, seekLen);
        if (consumeSource)
            v.stretchSrcPos += (v.direction > 0) ? seekLen : -seekLen;
    }

    v.stretchOutReadPos = 0;
    v.stretchOutAvail = 0;
}

static void fillStretchBlock (Voice& v, const SampleData& sample)
{
    if (v.stretchResetNeeded)
    {
        reseekStretcher (v, sample, false);
        v.stretchResetNeeded = false;
    }

    int inputSamples = (int) (kStretchBlockSize * v.stretchTimeRatio);
    if (inputSamples < 1) inputSamples = 1;
    const int maxInput = std::min ((int) v.stretchInBufL.size(), (int) v.stretchInBufR.size());
    if (maxInput <= 0)
        return;
    inputSamples = juce::jlimit (1, maxInput, inputSamples);

    for (int i = 0; i < inputSamples; ++i)
    {
        // Read from virtual looped source (with optional crossfade at loop boundaries)
        v.stretchInBufL[(size_t) i] = readCrossfadedSample (v, sample, v.stretchSrcPos, 0, v.direction);
        v.stretchInBufR[(size_t) i] = readCrossfadedSample (v, sample, v.stretchSrcPos, 1, v.direction);

        // Advance source position (loop/ping-pong wrap handled by advanceStretchSrcPos)
        auto action = advanceStretchSrcPos (v);
        bool stopAfterThisInput = false;

        // For non-loop modes, handle release/releaseTail boundaries
        switch (action)
        {
            case VoiceBoundaryAction::loopWrap:
            case VoiceBoundaryAction::pingPongTurnaround:
                v.stretchResetNeeded = true;
                inputSamples = i + 1;
                stopAfterThisInput = true;
                break;

            case VoiceBoundaryAction::releaseTail:
                v.stretchSrcPos = (v.direction > 0)
                    ? std::min (v.stretchSrcPos, (double) v.bufferEnd - 1)
                    : std::max (v.stretchSrcPos, 0.0);
                break;

            case VoiceBoundaryAction::release:
            {
                const float lastL = v.stretchInBufL[(size_t) i];
                const float lastR = v.stretchInBufR[(size_t) i];
                for (int j = i + 1; j < inputSamples; ++j)
                {
                    v.stretchInBufL[(size_t) j] = lastL;
                    v.stretchInBufR[(size_t) j] = lastR;
                }
                v.stretchSrcPos = (v.direction > 0)
                    ? (double) v.endSample
                    : (double) v.startSample;
                i = inputSamples;
                break;
            }

            default:
                break;
        }

        if (stopAfterThisInput)
            break;
    }

    const int outCapacity = std::min ((int) v.stretchOutBufL.size(), (int) v.stretchOutBufR.size());
    if (outCapacity <= 0)
    {
        v.stretchOutReadPos = 0;
        v.stretchOutAvail = 0;
        return;
    }
    const int outputSamples = std::min (kStretchBlockSize, outCapacity);

    // Process through Signalsmith
    float* inPtrs[2]  = { v.stretchInBufL.data(), v.stretchInBufR.data() };
    float* outPtrs[2] = { v.stretchOutBufL.data(), v.stretchOutBufR.data() };
    v.stretcher->process (inPtrs, inputSamples, outPtrs, outputSamples);

    v.stretchOutReadPos = 0;
    v.stretchOutAvail = outputSamples;
}

static void fillBungeeBlock (Voice& v, const SampleData& sample)
{
    if (! v.bungeeStretcher)
        return;

    auto& stretcher = *v.bungeeStretcher;

    // Set up the request for this grain
    Bungee::Request request;
    request.position = v.bungeeSrcPos;
    request.speed = v.bungeeSpeed;
    request.pitch = v.bungeePitch;
    request.reset = (v.bungeeOutAvail == 0 && v.bungeeOutReadPos == 0) || v.bungeeResetNeeded;
    request.resampleMode = resampleMode_autoOut;
    v.bungeeResetNeeded = false;

    if (request.reset)
        stretcher.preroll (request);

    stretcher.next (request);

    Bungee::InputChunk inputChunk = stretcher.specifyGrain (request);

    int numFrames = inputChunk.end - inputChunk.begin;
    const int maxIn = std::min (stretcher.maxInputFrameCount(),
                                (int) (v.bungeeInputBuf.size() / 2));
    if (maxIn <= 0 || numFrames <= 0)
    {
        v.bungeeOutReadPos = 0;
        v.bungeeOutAvail = 0;
        return;
    }
    if (numFrames > maxIn)
    {
        numFrames = maxIn;
        inputChunk.end = inputChunk.begin + numFrames;
    }

    const bool isLoopOrPingPong = v.looping || v.pingPong;
    int muteHead = 0, muteTail = 0;

    if (isLoopOrPingPong)
    {
        // Virtual loop: map each grain position through the exact loop helpers.
        // The virtual source is always valid, so muteHead/muteTail stay 0.
        // Use bungeeSpeed sign to determine crossfade direction for Bungee voices.
        const int bungeeDir = (v.bungeeSpeed >= 0.0) ? 1 : -1;
        for (int i = 0; i < numFrames; ++i)
        {
            double pos = inputChunk.begin + i;
            v.bungeeInputBuf[(size_t) i]         = readCrossfadedSample (v, sample, pos, 0, bungeeDir);
            v.bungeeInputBuf[(size_t)(maxIn + i)] = readCrossfadedSample (v, sample, pos, 1, bungeeDir);
        }
    }
    else
    {
        // Non-loop: original behavior with clamping and mute counts
        int effectiveEnd = v.releaseTail ? v.bufferEnd : v.endSample;

        for (int i = 0; i < numFrames; ++i)
        {
            double pos = inputChunk.begin + i;
            float sL = 0.0f, sR = 0.0f;
            if (pos >= v.startSample && pos < effectiveEnd)
            {
                sL = sample.getInterpolatedSample (pos, 0);
                sR = sample.getInterpolatedSample (pos, 1);
            }
            v.bungeeInputBuf[(size_t) i]         = sL;
            v.bungeeInputBuf[(size_t)(maxIn + i)] = sR;
        }

        if (inputChunk.begin < v.startSample)
            muteHead = v.startSample - inputChunk.begin;
        if (inputChunk.end > effectiveEnd)
            muteTail = inputChunk.end - effectiveEnd;
    }

    stretcher.analyseGrain (v.bungeeInputBuf.data(), (intptr_t) maxIn, muteHead, muteTail);

    Bungee::OutputChunk outputChunk;
    stretcher.synthesiseGrain (outputChunk);

    int outFrames = outputChunk.frameCount;
    if (outFrames > 0)
    {
        const float* outData = outputChunk.data;
        intptr_t stride = outputChunk.channelStride;
        const int outCapacity = std::min ((int) v.bungeeOutBufL.size(),
                                          (int) v.bungeeOutBufR.size());
        const int framesToCopy = std::min (outFrames, outCapacity);

        for (int i = 0; i < framesToCopy; ++i)
        {
            v.bungeeOutBufL[(size_t) i] = outData[i];
            v.bungeeOutBufR[(size_t) i] = outData[stride + i];
        }

        v.bungeeOutReadPos = 0;
        v.bungeeOutAvail = framesToCopy;
    }
    else
    {
        v.bungeeOutReadPos = 0;
        v.bungeeOutAvail = 0;
    }

    // Advance source position from the engine
    v.bungeeSrcPos = request.position;

    // Unbounded phase: do NOT canonicalize bungeeSrcPos back into loop range.
    // readExactLoopSample() maps grain read positions through the loop at read time.
    // This keeps Bungee's overlap-add state coherent across loop boundaries.
    if (v.pingPong)
    {
        // Flip speed only when crossing a boundary in the current travel direction
        if (v.bungeeSpeed > 0.0 && v.bungeeSrcPos >= (double) v.endSample)
            v.bungeeSpeed = -std::abs (v.bungeeSpeed);
        else if (v.bungeeSpeed < 0.0 && v.bungeeSrcPos < (double) v.startSample)
            v.bungeeSpeed = std::abs (v.bungeeSpeed);
    }
}

void VoicePool::processVoiceSample (int i, const SampleData& sample, double /*sr*/,
                                     float& outL, float& outR)
{
    auto& v = voices[i];
    outL = 0.0f;
    outR = 0.0f;

    if (! v.active)
        return;

    float voiceL = 0.0f, voiceR = 0.0f;

    if (v.stretchActive)
    {
        // Signalsmith Stretch processing
        float env = v.envelope.processSample();
        if (v.filterEnabled)
            v.filterEnvelope.processSample();

        if (v.envelope.isDone())
        {
            v.active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            xfadeSourcePositions[i].store (0.0f, std::memory_order_relaxed);
            return;
        }

        // Fill output buffer if empty
        if (v.stretchOutReadPos >= v.stretchOutAvail)
        {
            if (v.looping || v.pingPong)
            {
                // Loop/ping-pong: fillStretchBlock handles wrapping internally
                fillStretchBlock (v, sample);
            }
            else
            {
                switch (classifyBoundaryAction (v, v.stretchSrcPos, getPlaybackDirection ((double) v.direction), true))
                {
                    case VoiceBoundaryAction::releaseTail:
                        releaseVoiceIfNeeded (v);
                        fillStretchBlock (v, sample);
                        break;

                    case VoiceBoundaryAction::release:
                        releaseVoiceIfNeeded (v);
                        break;

                    case VoiceBoundaryAction::continuePlayback:
                    case VoiceBoundaryAction::loopWrap:
                    case VoiceBoundaryAction::pingPongTurnaround:
                        fillStretchBlock (v, sample);
                        break;
                }
            }
        }

        if (v.stretchOutReadPos < v.stretchOutAvail)
        {
            voiceL = v.stretchOutBufL[(size_t) v.stretchOutReadPos];
            voiceR = v.stretchOutBufR[(size_t) v.stretchOutReadPos];
            processVoiceFilter (v, (float) sampleRate, voiceL, voiceR);
            voiceL *= env * v.velocity * v.volume;
            voiceR *= env * v.velocity * v.volume;
            v.stretchOutReadPos++;
        }

        voicePositions[i].store ((float) v.stretchSrcPos, std::memory_order_relaxed);
        xfadeSourcePositions[i].store (
            computeXfadeSourceForUI (v, v.stretchSrcPos, getPlaybackDirection ((double) v.direction)),
            std::memory_order_relaxed);
    }
    else if (v.bungeeActive)
    {
        // Bungee Stretch processing
        float env = v.envelope.processSample();
        if (v.filterEnabled)
            v.filterEnvelope.processSample();

        if (v.envelope.isDone())
        {
            v.active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            xfadeSourcePositions[i].store (0.0f, std::memory_order_relaxed);
            return;
        }

        // Fill output buffer if empty
        if (v.bungeeOutReadPos >= v.bungeeOutAvail)
        {
            if (v.looping || v.pingPong)
            {
                // Loop/ping-pong: fillBungeeBlock handles wrapping internally
                fillBungeeBlock (v, sample);
            }
            else
            {
                switch (classifyBoundaryAction (v, v.bungeeSrcPos, getPlaybackDirection (v.bungeeSpeed), true))
                {
                    case VoiceBoundaryAction::releaseTail:
                        releaseVoiceIfNeeded (v);
                        fillBungeeBlock (v, sample);
                        break;

                    case VoiceBoundaryAction::release:
                        releaseVoiceIfNeeded (v);
                        break;

                    case VoiceBoundaryAction::continuePlayback:
                    case VoiceBoundaryAction::loopWrap:
                    case VoiceBoundaryAction::pingPongTurnaround:
                        fillBungeeBlock (v, sample);
                        break;
                }
            }
        }

        if (v.bungeeOutReadPos < v.bungeeOutAvail)
        {
            voiceL = v.bungeeOutBufL[(size_t) v.bungeeOutReadPos];
            voiceR = v.bungeeOutBufR[(size_t) v.bungeeOutReadPos];

            processVoiceFilter (v, (float) sampleRate, voiceL, voiceR);
            voiceL *= env * v.velocity * v.volume;
            voiceR *= env * v.velocity * v.volume;
            v.bungeeOutReadPos++;
        }

        // Wrap unbounded Bungee phase for UI cursor display only
        if (v.looping)
            voicePositions[i].store ((float) wrapLoopPosition (v.bungeeSrcPos, v.startSample, v.endSample), std::memory_order_relaxed);
        else if (v.pingPong)
            voicePositions[i].store ((float) reflectPingPongPosition (v.bungeeSrcPos, v.startSample, v.endSample), std::memory_order_relaxed);
        else
            voicePositions[i].store ((float) v.bungeeSrcPos, std::memory_order_relaxed);

        xfadeSourcePositions[i].store (
            computeXfadeSourceForUI (v, v.bungeeSrcPos, getPlaybackDirection (v.bungeeSpeed)),
            std::memory_order_relaxed);
    }
    else
    {
        // Process envelope
        float env = v.envelope.processSample();
        if (v.filterEnabled)
            v.filterEnvelope.processSample();

        if (v.envelope.isDone())
        {
            v.active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            xfadeSourcePositions[i].store (0.0f, std::memory_order_relaxed);
            return;
        }

        // Linear interpolation (with optional crossfade at loop boundaries)
        if (v.crossfadeLenSamples > 0)
        {
            voiceL = readRepitchCrossfadedSample (v, sample, v.position, 0, v.direction);
            voiceR = readRepitchCrossfadedSample (v, sample, v.position, 1, v.direction);
        }
        else
        {
            voiceL = readRepitchExactLoopSample (v, sample, v.position, 0);
            voiceR = readRepitchExactLoopSample (v, sample, v.position, 1);
        }
        processVoiceFilter (v, (float) sampleRate, voiceL, voiceR);
        voiceL *= env * v.velocity * v.volume;
        voiceR *= env * v.velocity * v.volume;

        // Advance position
        double newPos = v.position + v.speed * v.direction;

        switch (classifyBoundaryAction (v, newPos, getPlaybackDirection ((double) v.direction), false))
        {
            case VoiceBoundaryAction::continuePlayback:
                break;

            case VoiceBoundaryAction::pingPongTurnaround:
                if (v.direction > 0)
                {
                    newPos = v.endSample - 1;
                    v.direction = -1;
                }
                else
                {
                    newPos = v.startSample;
                    v.direction = 1;
                }
                break;

            case VoiceBoundaryAction::loopWrap:
            {
                const double len = (double) (v.endSample - v.startSample);
                if (len > 0)
                {
                    newPos = v.startSample + std::fmod (newPos - v.startSample, len);
                    if (newPos < v.startSample)
                        newPos += len;
                }
                break;
            }

            case VoiceBoundaryAction::releaseTail:
                releaseVoiceIfNeeded (v);
                break;

            case VoiceBoundaryAction::release:
                releaseVoiceIfNeeded (v);
                newPos = juce::jlimit ((double) v.startSample, (double) v.endSample - 1, newPos);
                break;
        }

        v.position = newPos;
        voicePositions[i].store ((float) v.position, std::memory_order_relaxed);
        xfadeSourcePositions[i].store (
            computeXfadeSourceForUI (v, v.position, getPlaybackDirection ((double) v.direction)),
            std::memory_order_relaxed);
    }

    outL = voiceL;
    outR = voiceR;
}

void VoicePool::processSample (const SampleData& sample, double sr,
                               float& outL, float& outR)
{
    outL = 0.0f;
    outR = 0.0f;

    for (int i = 0; i < maxActive; ++i)
    {
        float vL = 0.0f, vR = 0.0f;
        processVoiceSample (i, sample, sr, vL, vR);
        outL += vL;
        outR += vR;
    }

    // Always process the preview voice (used by LazyChopEngine)
    // even if it's outside the maxActive range
    constexpr int previewIdx = kPreviewVoiceIndex;
    if (previewIdx >= maxActive && voices[previewIdx].active)
    {
        float vL = 0.0f, vR = 0.0f;
        processVoiceSample (previewIdx, sample, sr, vL, vR);
        outL += vL;
        outR += vR;
    }
}

void VoicePool::renderMainBusBlock (const SampleData& sample,
                                     float* destL, float* destR, int numSamples)
{
    // Zero destination
    if (destL) std::fill_n (destL, numSamples, 0.0f);
    if (destR) std::fill_n (destR, numSamples, 0.0f);

    auto renderVoiceBlock = [&] (int vi)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            float vL = 0.0f, vR = 0.0f;
            processVoiceSample (vi, sample, sampleRate, vL, vR);
            if (destL) destL[s] += vL;
            if (destR) destR[s] += vR;
        }
    };

    for (int vi = 0; vi < maxActive; ++vi)
    {
        if (voices[vi].active)
            renderVoiceBlock (vi);
    }

    // Preview voice (LazyChopEngine / shift preview) — always on main bus
    constexpr int previewIdx = kPreviewVoiceIndex;
    if (previewIdx >= maxActive && voices[previewIdx].active)
        renderVoiceBlock (previewIdx);
}

void VoicePool::renderRoutedBlock (const SampleData& sample,
                                    float* busL[], float* busR[], int numBuses, int numSamples)
{
    const int scratchSize = (int) std::min (scratchL.size(), scratchR.size());
    jassert (scratchSize > 0);
    if (scratchSize <= 0 || numSamples <= 0)
        return;

    auto renderVoiceToScratch = [&] (int vi, int chunkSamples)
    {
        for (int s = 0; s < chunkSamples; ++s)
            processVoiceSample (vi, sample, sampleRate, scratchL[s], scratchR[s]);
    };

    auto accumulateScratchToBus = [&] (float* dstL, float* dstR, int chunkStart, int chunkSamples)
    {
        for (int s = 0; s < chunkSamples; ++s)
        {
            if (dstL) dstL[chunkStart + s] += scratchL[s];
            if (dstR) dstR[chunkStart + s] += scratchR[s];
        }
    };

    for (int chunkStart = 0; chunkStart < numSamples; chunkStart += scratchSize)
    {
        const int chunkSamples = std::min (scratchSize, numSamples - chunkStart);

        for (int vi = 0; vi < maxActive; ++vi)
        {
            if (! voices[vi].active)
                continue;

            renderVoiceToScratch (vi, chunkSamples);

            int bus = voices[vi].outputBus;
            if (bus < 0 || bus >= numBuses || busL[bus] == nullptr)
                bus = 0;

            accumulateScratchToBus (busL[bus], busR[bus], chunkStart, chunkSamples);
        }

        // Preview voice — always to bus 0
        constexpr int previewIdx = kPreviewVoiceIndex;
        if (previewIdx >= maxActive && voices[previewIdx].active)
        {
            renderVoiceToScratch (previewIdx, chunkSamples);
            accumulateScratchToBus (busL[0], busR[0], chunkStart, chunkSamples);
        }
    }
}

void VoicePool::startShiftPreview (int startSample, int bufferSize,
                                    const PreviewStretchParams& p)
{
    // Shares the lazyChop preview slot; only called when lazyChop is inactive
    const int i = kPreviewVoiceIndex;
    Voice& v = voices[i];
    initPreviewVoiceCommon (v, startSample, startSample, bufferSize, false, 0.8f);
    initPreviewVoiceStretch (v, startSample, p);

    v.envelope.noteOn (0.002f, 0.0f, 1.0f, 0.05f, p.sampleRate);
    voicePositions[i].store ((float) startSample, std::memory_order_relaxed);
    xfadeSourcePositions[i].store (0.0f, std::memory_order_relaxed);
}

void VoicePool::stopShiftPreview()
{
    int i = kPreviewVoiceIndex;
    if (voices[i].active)
        voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
}
