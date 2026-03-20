#include "VoicePool.h"

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

static inline float dbToLinear (float dB)
{
    if (dB <= -100.0f) return 0.0f;
    return std::pow (10.0f, dB / 20.0f);
}

static inline float saturateSample (float x, float drivePercent)
{
    if (drivePercent <= 0.0f)
        return x;

    const float norm = juce::jlimit (0.0f, 1.0f, drivePercent / 100.0f);
    const float gain = 1.0f + norm * 12.0f;
    const float tanhGain = std::tanh (gain);
    if (std::abs (tanhGain) < 1.0e-6f)
        return x;
    return std::tanh (x * gain) / tanhGain;
}

static inline float resonancePercentToQ (float resonancePercent)
{
    const float norm = juce::jlimit (0.0f, 1.0f, resonancePercent / 100.0f);
    return 0.7071f + norm * 11.2929f;
}

static inline float computeFilterCutoff (const Voice& v, float sampleRate)
{
    const float keyTrackedCutoff = v.filterCutoff * v.filterKeyTrackRatio;
    const float envLevel = v.filterEnvelope.getLevel();
    const float cutoff = keyTrackedCutoff * std::pow (2.0f, v.filterEnvAmount * envLevel / 12.0f);
    return juce::jlimit (20.0f, sampleRate * 0.49f, cutoff);
}

static inline void processVoiceFilter (Voice& v, float sampleRate, float& inOutL, float& inOutR)
{
    if (! v.filterEnabled)
        return;

    const float q = resonancePercentToQ (v.filterReso);
    const float cutoff = computeFilterCutoff (v, sampleRate);
    inOutL = saturateSample (inOutL, v.filterDrive);
    inOutR = saturateSample (inOutR, v.filterDrive);
    const auto coeffs = SvfFilter::computeCoeffs (cutoff, q, sampleRate);

    float yL = v.filterL1.process (inOutL, coeffs, v.filterType);
    float yR = v.filterR1.process (inOutR, coeffs, v.filterType);

    if (v.filterSlope > 0)
    {
        yL = v.filterL2.process (yL, coeffs, v.filterType);
        yR = v.filterR2.process (yR, coeffs, v.filterType);
    }

    inOutL = yL;
    inOutR = yR;
}

VoicePool::VoicePool()
{
    for (auto& p : voicePositions)
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
        v.bungeePPFadeL.resize ((size_t) v.bungeePPFadeLen);
        v.bungeePPFadeR.resize ((size_t) v.bungeePPFadeLen);
    }
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
        }
    }
    maxActive = n;
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

    // Pre-roll: prime the pipeline so first output isn't silence
    float playbackRate = v.stretchTimeRatio;
    int seekLen = v.stretcher->outputSeekLength (playbackRate);
    seekLen = std::min (seekLen, v.endSample - v.startSample);
    seekLen = juce::jlimit (0, (int) v.stretchInBufL.size(), seekLen);

    int maxFrame = sample.getNumFrames() - 1;
    if (seekLen > 0 && sample.isLoaded() && maxFrame >= 0)
    {
        for (int i = 0; i < seekLen; ++i)
        {
            int srcIdx = (v.direction > 0)
                ? v.startSample + i
                : v.endSample - 1 - i;
            srcIdx = juce::jlimit (0, maxFrame, srcIdx);
            v.stretchInBufL[(size_t) i] = sample.getInterpolatedSample (srcIdx, 0);
            v.stretchInBufR[(size_t) i] = sample.getInterpolatedSample (srcIdx, 1);
        }
        float* ptrs[2] = { v.stretchInBufL.data(), v.stretchInBufR.data() };
        v.stretcher->outputSeek (ptrs, seekLen);
        if (v.direction > 0)
            v.stretchSrcPos = v.startSample + seekLen;
        else
            v.stretchSrcPos = v.endSample - 1 - seekLen;
    }
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
    v.bungeePPFade = 0;
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
    const float keyTrackPercent = sm.resolveParam (sliceIdx, kLockFilterKeyTrack,
                                                   s.filterKeyTrack, p.globalFilterKeyTrack);
    const float keyTrackNorm = juce::jlimit (0.0f, 1.0f, keyTrackPercent / 100.0f);
    const float noteRatio = std::pow (2.0f, ((float) p.note - (float) p.rootNote) / 12.0f);
    v.filterKeyTrackRatio = 1.0f + (noteRatio - 1.0f) * keyTrackNorm;
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
    v.filterL1.reset();
    v.filterR1.reset();
    v.filterL2.reset();
    v.filterR2.reset();
    v.filterEnvelope.noteOn (filterEnvAttack, filterEnvDecay, filterEnvSustain, filterEnvRelease, sampleRate);

    // Reset stretch state and ping-pong fade (guard against stale data from stolen voices)
    v.stretchActive  = false;
    v.bungeeActive   = false;
    v.bungeePPFade   = 0;
    std::fill (v.bungeePPFadeL.begin(), v.bungeePPFadeL.end(), 0.0f);
    std::fill (v.bungeePPFadeR.begin(), v.bungeePPFadeR.end(), 0.0f);

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

static void fillStretchBlock (Voice& v, const SampleData& sample)
{
    int inputSamples = (int) (kStretchBlockSize * v.stretchTimeRatio);
    if (inputSamples < 1) inputSamples = 1;
    const int maxInput = std::min ((int) v.stretchInBufL.size(), (int) v.stretchInBufR.size());
    if (maxInput <= 0)
        return;
    inputSamples = juce::jlimit (1, maxInput, inputSamples);

    for (int i = 0; i < inputSamples; ++i)
    {
        double pos = v.stretchSrcPos;
        // Clamp to sample buffer bounds for safety
        pos = juce::jlimit (0.0, (double) v.bufferEnd - 1, pos);
        v.stretchInBufL[(size_t) i] = sample.getInterpolatedSample (pos, 0);
        v.stretchInBufR[(size_t) i] = sample.getInterpolatedSample (pos, 1);
        v.stretchSrcPos += (double) v.direction;

        // Check bounds
        if (v.direction > 0 && v.stretchSrcPos >= v.endSample)
        {
            if (v.pingPong)
            {
                v.stretchSrcPos = v.endSample - 1;
                v.direction = -1;
            }
            else if (v.looping)
            {
                v.stretchSrcPos = v.startSample;
            }
            else if (v.releaseTail && v.stretchSrcPos < v.bufferEnd)
            {
                v.stretchSrcPos = std::min (v.stretchSrcPos, (double) v.bufferEnd - 1);
            }
            else
            {
                float lastL = v.stretchInBufL[(size_t) i];
                float lastR = v.stretchInBufR[(size_t) i];
                for (int j = i + 1; j < inputSamples; ++j)
                {
                    v.stretchInBufL[(size_t) j] = lastL;
                    v.stretchInBufR[(size_t) j] = lastR;
                }
                v.stretchSrcPos = v.endSample;
                break;
            }
        }
        else if (v.direction < 0 && v.stretchSrcPos <= v.startSample)
        {
            if (v.pingPong)
            {
                v.stretchSrcPos = v.startSample;
                v.direction = 1;
            }
            else if (v.looping)
            {
                v.stretchSrcPos = v.endSample - 1;
            }
            else if (v.releaseTail && v.stretchSrcPos >= 0)
            {
                v.stretchSrcPos = std::max (v.stretchSrcPos, 0.0);
            }
            else
            {
                float lastL = v.stretchInBufL[(size_t) i];
                float lastR = v.stretchInBufR[(size_t) i];
                for (int j = i + 1; j < inputSamples; ++j)
                {
                    v.stretchInBufL[(size_t) j] = lastL;
                    v.stretchInBufR[(size_t) j] = lastR;
                }
                v.stretchSrcPos = v.startSample;
                break;
            }
        }
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

    // Boundary checks before requesting next grain
    if (v.pingPong)
    {
        if (v.bungeeSpeed > 0.0 && v.bungeeSrcPos >= v.endSample)
        {
            v.bungeeSrcPos = v.endSample - 1;
            v.bungeeSpeed = -std::abs (v.bungeeSpeed);
        }
        else if (v.bungeeSpeed < 0.0 && v.bungeeSrcPos <= v.startSample)
        {
            v.bungeeSrcPos = v.startSample;
            v.bungeeSpeed = std::abs (v.bungeeSpeed);
        }
    }
    else if (v.looping)
    {
        if (v.bungeeSpeed > 0.0 && v.bungeeSrcPos >= v.endSample)
            v.bungeeSrcPos = v.startSample;
        else if (v.bungeeSpeed < 0.0 && v.bungeeSrcPos <= v.startSample)
            v.bungeeSrcPos = v.endSample - 1;
    }

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

    // Process grains until we have output
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

    // Determine effective end for reading (release tail allows reading past slice end)
    int effectiveEnd = v.releaseTail && !v.pingPong ? v.bufferEnd : v.endSample;

    // Fill input buffer (non-interleaved: ch0 then ch1)
    for (int i = 0; i < numFrames; ++i)
    {
        double pos = inputChunk.begin + i;

        float sL = 0.0f, sR = 0.0f;
        if (pos >= v.startSample && pos < effectiveEnd)
        {
            sL = sample.getInterpolatedSample (pos, 0);
            sR = sample.getInterpolatedSample (pos, 1);
        }
        v.bungeeInputBuf[(size_t) i] = sL;
        v.bungeeInputBuf[(size_t)(maxIn + i)] = sR;
    }

    // Compute mute counts
    int muteHead = 0, muteTail = 0;
    if (inputChunk.begin < v.startSample)
        muteHead = v.startSample - inputChunk.begin;
    if (inputChunk.end > effectiveEnd)
        muteTail = inputChunk.end - effectiveEnd;

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

    // Advance source position
    v.bungeeSrcPos = request.position;

    // Clamp to slice boundaries to prevent playhead overshoot
    if (v.pingPong)
    {
        if (v.bungeeSrcPos > v.endSample)
            v.bungeeSrcPos = v.endSample;
        if (v.bungeeSrcPos < v.startSample)
            v.bungeeSrcPos = v.startSample;
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
            return;
        }

        // Fill output buffer if empty
        if (v.stretchOutReadPos >= v.stretchOutAvail)
        {
            bool pastEnd = (v.direction > 0)
                ? (v.stretchSrcPos >= v.endSample)
                : (v.stretchSrcPos <= v.startSample);

            if (pastEnd && !v.pingPong)
            {
                if (v.looping)
                {
                    if (v.direction > 0)
                        v.stretchSrcPos = v.startSample;
                    else
                        v.stretchSrcPos = v.endSample - 1;
                    fillStretchBlock (v, sample);
                }
                else if (v.releaseTail && v.stretchSrcPos < v.bufferEnd && v.stretchSrcPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }
                    fillStretchBlock (v, sample);
                }
                else
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }
                }
            }
            else
            {
                fillStretchBlock (v, sample);
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
            return;
        }

        // Fill output buffer if empty
        if (v.bungeeOutReadPos >= v.bungeeOutAvail)
        {
            bool pastEnd = (v.bungeeSpeed > 0.0)
                ? (v.bungeeSrcPos >= v.endSample)
                : (v.bungeeSrcPos <= v.startSample);

            if (pastEnd && !v.pingPong)
            {
                if (v.looping)
                {
                    if (v.bungeeSpeed > 0.0)
                        v.bungeeSrcPos = v.startSample;
                    else
                        v.bungeeSrcPos = v.endSample - 1;
                    v.bungeeResetNeeded = true;
                    fillBungeeBlock (v, sample);
                }
                else if (v.releaseTail && v.bungeeSrcPos < v.bufferEnd && v.bungeeSrcPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }
                    fillBungeeBlock (v, sample);
                }
                else
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }
                }
            }
            else
            {
                fillBungeeBlock (v, sample);
            }
        }

        if (v.bungeeOutReadPos < v.bungeeOutAvail)
        {
            voiceL = v.bungeeOutBufL[(size_t) v.bungeeOutReadPos];
            voiceR = v.bungeeOutBufR[(size_t) v.bungeeOutReadPos];

            // Crossfade during ping-pong direction change
            if (v.bungeePPFade > 0)
            {
                int fadeIdx = v.bungeePPFadeLen - v.bungeePPFade;
                float fadeIn = (float) fadeIdx / (float) v.bungeePPFadeLen;
                float fadeOut = 1.0f - fadeIn;

                if (fadeIdx < (int) v.bungeePPFadeL.size())
                {
                    voiceL = voiceL * fadeIn + v.bungeePPFadeL[(size_t) fadeIdx] * fadeOut;
                    voiceR = voiceR * fadeIn + v.bungeePPFadeR[(size_t) fadeIdx] * fadeOut;
                }
                v.bungeePPFade--;
            }

            processVoiceFilter (v, (float) sampleRate, voiceL, voiceR);
            voiceL *= env * v.velocity * v.volume;
            voiceR *= env * v.velocity * v.volume;
            v.bungeeOutReadPos++;
        }

        voicePositions[i].store ((float) v.bungeeSrcPos, std::memory_order_relaxed);
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
            return;
        }

        // Linear interpolation
        voiceL = sample.getInterpolatedSample (v.position, 0);
        voiceR = sample.getInterpolatedSample (v.position, 1);
        processVoiceFilter (v, (float) sampleRate, voiceL, voiceR);
        voiceL *= env * v.velocity * v.volume;
        voiceR *= env * v.velocity * v.volume;

        // Advance position
        double newPos = v.position + v.speed * v.direction;

        if (v.pingPong)
        {
            if (newPos >= v.endSample)
            {
                newPos = v.endSample - 1;
                v.direction = -1;
            }
            else if (newPos < v.startSample)
            {
                newPos = v.startSample;
                v.direction = 1;
            }
        }
        else
        {
            if (newPos >= v.endSample || newPos < v.startSample)
            {
                if (v.looping)
                {
                    double len = (double) (v.endSample - v.startSample);
                    if (len > 0)
                        newPos = v.startSample + std::fmod (newPos - v.startSample, len);
                    if (newPos < v.startSample)
                        newPos += len;
                }
                else if (v.releaseTail && newPos >= v.endSample && newPos < v.bufferEnd)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }
                }
                else if (v.releaseTail && v.direction < 0 && newPos < v.startSample && newPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }
                }
                else
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                    {
                        v.envelope.noteOff();
                        v.filterEnvelope.noteOff();
                    }

                    newPos = juce::jlimit ((double) v.startSample, (double) v.endSample - 1, newPos);
                }
            }
        }

        v.position = newPos;
        voicePositions[i].store ((float) v.position, std::memory_order_relaxed);
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

void VoicePool::startShiftPreview (int startSample, int bufferSize,
                                    const PreviewStretchParams& p)
{
    // Shares the lazyChop preview slot; only called when lazyChop is inactive
    const int i = kPreviewVoiceIndex;
    Voice& v = voices[i];
    v.active        = true;
    v.sliceIdx      = -1;
    v.position      = (double) startSample;
    v.speed         = 1.0;
    v.direction     = 1;
    v.midiNote      = -1;
    v.velocity      = 0.8f;
    v.startSample   = startSample;
    v.endSample     = bufferSize;
    v.bufferEnd     = bufferSize;
    v.pingPong      = false;
    v.muteGroup     = 0;
    v.looping       = false;
    v.volume        = 1.0f;
    v.releaseTail   = false;
    v.oneShot       = false;
    v.filterEnabled = false;
    v.filterCutoff  = 8200.0f;
    v.filterReso    = 0.0f;
    v.filterDrive   = 0.0f;
    v.filterEnvAmount = 0.0f;
    v.filterKeyTrackRatio = 1.0f;
    v.filterL1.reset();
    v.filterR1.reset();
    v.filterL2.reset();
    v.filterR2.reset();
    v.stretchActive = false;
    v.stretchOutReadPos = 0;
    v.stretchOutAvail   = 0;
    v.bungeeActive  = false;
    v.bungeeOutReadPos = 0;
    v.bungeeOutAvail   = 0;
    v.bungeePPFade  = 0;

    if (p.stretchEnabled && p.dawBpm > 0.0f && p.bpm > 0.0f)
    {
        const float speedRatio = p.dawBpm / p.bpm;
        if (p.algorithm == 0)
        {
            v.speed = speedRatio;
        }
        else if (p.algorithm == 2 && p.sample != nullptr)
        {
            v.bungeeActive = true;
            v.bungeeSpeed = (double) speedRatio;
            v.bungeeSrcPos = startSample;
            initBungee (v, p.pitch, p.sampleRate, juce::jlimit (-1, 1, p.grainMode - 1));
        }
        else if (p.sample != nullptr)
        {
            v.stretchActive = true;
            v.stretchTimeRatio = speedRatio;
            v.stretchPitchSemis = p.pitch;
            v.stretchSrcPos = startSample;
            initStretcher (v, p.pitch, p.sampleRate,
                           p.tonality, p.formant, p.formantComp, *p.sample);
        }
    }
    else if (p.algorithm == 1 && p.sample != nullptr)
    {
        v.stretchActive = true;
        v.stretchTimeRatio = 1.0f;
        v.stretchPitchSemis = p.pitch;
        v.stretchSrcPos = startSample;
        initStretcher (v, p.pitch, p.sampleRate,
                       p.tonality, p.formant, p.formantComp, *p.sample);
    }
    else if (p.algorithm == 2 && p.sample != nullptr)
    {
        v.bungeeActive = true;
        v.bungeeSpeed = 1.0;
        v.bungeeSrcPos = startSample;
        initBungee (v, p.pitch, p.sampleRate, juce::jlimit (-1, 1, p.grainMode - 1));
    }
    else
    {
        v.speed = std::pow (2.0f, p.pitch / 12.0f);
    }

    v.envelope.noteOn (0.002f, 0.0f, 1.0f, 0.05f, p.sampleRate);
    voicePositions[i].store ((float) startSample, std::memory_order_relaxed);
}

void VoicePool::stopShiftPreview()
{
    int i = kPreviewVoiceIndex;
    if (voices[i].active)
        voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
}

void VoicePool::processSampleMultiOut (const SampleData& sample, double sr,
                                        float* outPtrs[], int /*numOuts*/)
{
    // This method is not used directly — multi-out routing is handled in processBlock
    // by calling processVoiceSample per voice and routing to the correct bus.
    (void) sample;
    (void) sr;
    (void) outPtrs;
}
