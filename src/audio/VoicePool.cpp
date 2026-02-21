#include "VoicePool.h"

// Include Signalsmith Stretch
#include "signalsmith-stretch.h"

// Include Bungee
#include "bungee/Bungee.h"

#include <algorithm>
#include <cmath>

static constexpr int kStretchBlockSize = 128;

static inline float dbToLinear (float dB)
{
    if (dB <= -100.0f) return 0.0f;
    return std::pow (10.0f, dB / 20.0f);
}

VoicePool::VoicePool()
{
    for (auto& p : voicePositions)
        p.store (0.0f, std::memory_order_relaxed);
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
    n = juce::jlimit (1, kMaxVoices, n);
    if (n < maxActive)
    {
        // Kill voices beyond new limit (but preserve preview voice)
        constexpr int previewIdx = kMaxVoices - 1;
        for (int i = n; i < maxActive; ++i)
        {
            if (i == previewIdx) continue;
            voices[i].active = false;
            voices[i].stretcher.reset();
            voices[i].bungeeStretcher.reset();
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

    v.stretcher = std::make_shared<signalsmith::stretch::SignalsmithStretch<float, void>>();
    v.stretcher->configure (2, blockSize, interval, false);

    float tonalityLimit = (tonalityHz > 0.0f && sr > 0.0) ? (float)(tonalityHz / sr) : 0.0f;
    v.stretcher->setTransposeSemitones (pitchSemis, tonalityLimit);

    if (formantSemis != 0.0f || formantComp)
        v.stretcher->setFormantSemitones (formantSemis, formantComp);

    v.stretchInBufL.resize (kStretchBlockSize);
    v.stretchInBufR.resize (kStretchBlockSize);
    v.stretchOutBufL.resize (kStretchBlockSize);
    v.stretchOutBufR.resize (kStretchBlockSize);
    v.stretchOutReadPos = 0;
    v.stretchOutAvail = 0;

    // Pre-roll: prime the pipeline so first output isn't silence
    float playbackRate = v.stretchTimeRatio;
    int seekLen = v.stretcher->outputSeekLength (playbackRate);
    seekLen = std::min (seekLen, v.endSample - v.startSample);

    int maxFrame = sample.getNumFrames() - 1;
    if (seekLen > 0 && sample.isLoaded() && maxFrame >= 0)
    {
        std::vector<float> seekL ((size_t) seekLen), seekR ((size_t) seekLen);
        for (int i = 0; i < seekLen; ++i)
        {
            int srcIdx = (v.direction > 0)
                ? v.startSample + i
                : v.endSample - 1 - i;
            srcIdx = juce::jlimit (0, maxFrame, srcIdx);
            seekL[(size_t) i] = sample.getInterpolatedSample (srcIdx, 0);
            seekR[(size_t) i] = sample.getInterpolatedSample (srcIdx, 1);
        }
        float* ptrs[2] = { seekL.data(), seekR.data() };
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
    v.bungeeOutBufL.clear();
    v.bungeeOutBufR.clear();
    v.bungeeOutReadPos = 0;
    v.bungeeOutAvail = 0;
    v.bungeePPFade = 0;

    int maxIn = v.bungeeStretcher->maxInputFrameCount();
    v.bungeeInputBuf.resize ((size_t) maxIn * 2);
}

void VoicePool::startVoice (int voiceIdx, int sliceIdx, float velocity, int note,
                            SliceManager& sm,
                            float globalBpm, float globalPitch, int globalAlgorithm,
                            float globalAttack, float globalDecay, float globalSustain, float globalRelease,
                            int globalMuteGroup,
                            bool globalStretchEnabled, float dawBpmVal,
                            float globalTonality, float globalFormant, bool globalFormantComp,
                            int globalGrainMode, float globalVolume,
                            bool globalReleaseTail,
                            bool globalReverse,
                            int globalLoopMode,
                            const SampleData& sample)
{
    auto& v = voices[voiceIdx];
    const auto& s = sm.getSlice (sliceIdx);

    v.active    = true;
    v.sliceIdx  = sliceIdx;
    v.midiNote  = note;
    v.velocity  = velocity / 127.0f;
    v.age       = 0;

    v.startSample = s.startSample;
    v.endSample   = s.endSample;

    // Resolve parameters via inheritance
    float attack   = sm.resolveParam (sliceIdx, kLockAttack,    s.attackSec,      globalAttack);
    float decay    = sm.resolveParam (sliceIdx, kLockDecay,     s.decaySec,       globalDecay);
    float sustain  = sm.resolveParam (sliceIdx, kLockSustain,   s.sustainLevel,   globalSustain);
    float release  = sm.resolveParam (sliceIdx, kLockRelease,   s.releaseSec,     globalRelease);

    v.envelope.noteOn (attack, decay, sustain, release);

    int resolvedLoopMode = (int) sm.resolveParam (sliceIdx, kLockLoop, (float) s.loopMode, (float) globalLoopMode);
    v.looping    = (resolvedLoopMode == 1);
    v.pingPong   = (resolvedLoopMode == 2);
    v.muteGroup  = (int) sm.resolveParam (sliceIdx, kLockMuteGroup, (float) s.muteGroup, (float) globalMuteGroup);

    bool rev = sm.resolveParam (sliceIdx, kLockReverse,
                                 s.reverse ? 1.0f : 0.0f,
                                 globalReverse ? 1.0f : 0.0f) > 0.5f;
    v.direction = rev ? -1 : 1;
    v.position  = rev ? (s.endSample - 1) : s.startSample;

    v.outputBus = (int) sm.resolveParam (sliceIdx, kLockOutputBus, (float) s.outputBus, 0.0f);

    int algo = (int) sm.resolveParam (sliceIdx, kLockAlgorithm, (float) s.algorithm, (float) globalAlgorithm);

    float sliceBpm = sm.resolveParam (sliceIdx, kLockBpm, s.bpm, globalBpm);
    float pitch = sm.resolveParam (sliceIdx, kLockPitch, s.pitchSemitones, globalPitch);
    float pitchRatio = std::pow (2.0f, pitch / 12.0f);

    bool stretchOn = sm.resolveParam (sliceIdx, kLockStretch,
                                       s.stretchEnabled ? 1.0f : 0.0f,
                                       globalStretchEnabled ? 1.0f : 0.0f) > 0.5f;

    float tonality = sm.resolveParam (sliceIdx, kLockTonality, s.tonalityHz, globalTonality);
    float formant = sm.resolveParam (sliceIdx, kLockFormant, s.formantSemitones, globalFormant);
    bool fComp = sm.resolveParam (sliceIdx, kLockFormantComp,
                                   s.formantComp ? 1.0f : 0.0f,
                                   globalFormantComp ? 1.0f : 0.0f) > 0.5f;

    int grainMode = (int) sm.resolveParam (sliceIdx, kLockGrainMode,
                                           (float) s.grainMode, (float) globalGrainMode);
    // Convert grainMode choice index (0=Fast, 1=Normal, 2=Smooth) to log2 hop adjust (-1, 0, +1)
    int hopAdj = grainMode - 1;

    v.volume = dbToLinear (sm.resolveParam (sliceIdx, kLockVolume, s.volume, globalVolume));

    v.releaseTail = sm.resolveParam (sliceIdx, kLockReleaseTail,
                                      s.releaseTail ? 1.0f : 0.0f,
                                      globalReleaseTail ? 1.0f : 0.0f) > 0.5f;
    v.sampleEnd = sample.getNumFrames();

    // Reset stretch state
    v.stretchActive = false;
    v.bungeeActive = false;

    if (stretchOn && dawBpmVal > 0.0f && sliceBpm > 0.0f)
    {
        float speedRatio = dawBpmVal / sliceBpm;

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
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[i].active && voices[i].midiNote == note)
            voices[i].envelope.noteOff();
}

void VoicePool::muteGroup (int group, int exceptVoice)
{
    if (group <= 0)
        return;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        if (i != exceptVoice && voices[i].active && voices[i].muteGroup == group)
            voices[i].envelope.forceRelease (0.005f);
    }
}

static void fillStretchBlock (Voice& v, const SampleData& sample)
{
    int inputSamples = (int) (kStretchBlockSize * v.stretchTimeRatio);
    if (inputSamples < 1) inputSamples = 1;

    v.stretchInBufL.resize ((size_t) inputSamples);
    v.stretchInBufR.resize ((size_t) inputSamples);

    for (int i = 0; i < inputSamples; ++i)
    {
        double pos = v.stretchSrcPos;
        // Clamp to sample buffer bounds for safety
        pos = juce::jlimit (0.0, (double) v.sampleEnd - 1, pos);
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
            else if (v.releaseTail && v.stretchSrcPos < v.sampleEnd)
            {
                v.stretchSrcPos = std::min (v.stretchSrcPos, (double) v.sampleEnd - 1);
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

    v.stretchOutBufL.resize (kStretchBlockSize);
    v.stretchOutBufR.resize (kStretchBlockSize);

    // Process through Signalsmith
    float* inPtrs[2]  = { v.stretchInBufL.data(), v.stretchInBufR.data() };
    float* outPtrs[2] = { v.stretchOutBufL.data(), v.stretchOutBufR.data() };
    v.stretcher->process (inPtrs, inputSamples, outPtrs, kStretchBlockSize);

    v.stretchOutReadPos = 0;
    v.stretchOutAvail = kStretchBlockSize;
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
    int maxIn = stretcher.maxInputFrameCount();
    v.bungeeInputBuf.resize ((size_t) maxIn * 2);

    // Determine effective end for reading (release tail allows reading past slice end)
    int effectiveEnd = v.releaseTail && !v.pingPong ? v.sampleEnd : v.endSample;

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
        v.bungeeOutBufL.resize ((size_t) outFrames);
        v.bungeeOutBufR.resize ((size_t) outFrames);

        const float* outData = outputChunk.data;
        intptr_t stride = outputChunk.channelStride;

        for (int i = 0; i < outFrames; ++i)
        {
            v.bungeeOutBufL[(size_t) i] = outData[i];
            v.bungeeOutBufR[(size_t) i] = outData[stride + i];
        }

        v.bungeeOutReadPos = 0;
        v.bungeeOutAvail = outFrames;
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

void VoicePool::processVoiceSample (int i, const SampleData& sample, double sr,
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
        float env = v.envelope.processSample (sr);

        if (v.envelope.isDone())
        {
            v.active = false;
            v.stretcher.reset();
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
                else if (v.releaseTail && v.stretchSrcPos < v.sampleEnd && v.stretchSrcPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                    fillStretchBlock (v, sample);
                }
                else
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                }
            }
            else
            {
                fillStretchBlock (v, sample);
            }
        }

        if (v.stretchOutReadPos < v.stretchOutAvail)
        {
            voiceL = v.stretchOutBufL[(size_t) v.stretchOutReadPos] * env * v.velocity * v.volume;
            voiceR = v.stretchOutBufR[(size_t) v.stretchOutReadPos] * env * v.velocity * v.volume;
            v.stretchOutReadPos++;
        }

        v.age++;
        voicePositions[i].store ((float) v.stretchSrcPos, std::memory_order_relaxed);
    }
    else if (v.bungeeActive)
    {
        // Bungee Stretch processing
        float env = v.envelope.processSample (sr);

        if (v.envelope.isDone())
        {
            v.active = false;
            v.bungeeStretcher.reset();
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
                else if (v.releaseTail && v.bungeeSrcPos < v.sampleEnd && v.bungeeSrcPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                    fillBungeeBlock (v, sample);
                }
                else
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                }
            }
            else
            {
                fillBungeeBlock (v, sample);
            }
        }

        if (v.bungeeOutReadPos < v.bungeeOutAvail)
        {
            voiceL = v.bungeeOutBufL[(size_t) v.bungeeOutReadPos] * env * v.velocity * v.volume;
            voiceR = v.bungeeOutBufR[(size_t) v.bungeeOutReadPos] * env * v.velocity * v.volume;

            // Crossfade during ping-pong direction change
            if (v.bungeePPFade > 0)
            {
                int fadeIdx = v.bungeePPFadeLen - v.bungeePPFade;
                float fadeIn = (float) fadeIdx / (float) v.bungeePPFadeLen;
                float fadeOut = 1.0f - fadeIn;

                if (fadeIdx < (int) v.bungeePPFadeL.size())
                {
                    voiceL = voiceL * fadeIn + v.bungeePPFadeL[(size_t) fadeIdx] * env * v.velocity * v.volume * fadeOut;
                    voiceR = voiceR * fadeIn + v.bungeePPFadeR[(size_t) fadeIdx] * env * v.velocity * v.volume * fadeOut;
                }
                v.bungeePPFade--;
            }

            v.bungeeOutReadPos++;
        }

        v.age++;
        voicePositions[i].store ((float) v.bungeeSrcPos, std::memory_order_relaxed);
    }
    else
    {
        // Process envelope
        float env = v.envelope.processSample (sr);

        if (v.envelope.isDone())
        {
            v.active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            return;
        }

        // Linear interpolation
        voiceL = sample.getInterpolatedSample (v.position, 0) * env * v.velocity * v.volume;
        voiceR = sample.getInterpolatedSample (v.position, 1) * env * v.velocity * v.volume;

        v.age++;

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
                else if (v.releaseTail && newPos >= v.endSample && newPos < v.sampleEnd)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                }
                else if (v.releaseTail && v.direction < 0 && newPos < v.startSample && newPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                }
                else
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();

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
    constexpr int previewIdx = kMaxVoices - 1;
    if (previewIdx >= maxActive && voices[previewIdx].active)
    {
        float vL = 0.0f, vR = 0.0f;
        processVoiceSample (previewIdx, sample, sr, vL, vR);
        outL += vL;
        outR += vR;
    }
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
