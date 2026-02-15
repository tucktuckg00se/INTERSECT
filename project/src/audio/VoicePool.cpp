#include "VoicePool.h"
#include "GrainEngine.h"

// Include Signalsmith Stretch
#include "signalsmith-stretch.h"

// Include Bungee
#include "bungee/Bungee.h"

#include <cmath>

static constexpr int kStretchBlockSize = 128;

VoicePool::VoicePool()
{
    for (auto& p : voicePositions)
        p.store (0.0f, std::memory_order_relaxed);
}

int VoicePool::allocate()
{
    // First pass: find inactive voice
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[i].active)
            return i;

    // Second pass: steal — prefer releasing voices with lowest envelope
    int best = 0;
    float bestScore = 999999.0f;

    for (int i = 0; i < kMaxVoices; ++i)
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

    if (seekLen > 0 && sample.isLoaded())
    {
        std::vector<float> seekL ((size_t) seekLen), seekR ((size_t) seekLen);
        for (int i = 0; i < seekLen; ++i)
        {
            seekL[(size_t) i] = sample.getInterpolatedSample (v.startSample + i, 0);
            seekR[(size_t) i] = sample.getInterpolatedSample (v.startSample + i, 1);
        }
        float* ptrs[2] = { seekL.data(), seekR.data() };
        v.stretcher->outputSeek (ptrs, seekLen);
        v.stretchSrcPos = v.startSample + seekLen;
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

    int maxIn = v.bungeeStretcher->maxInputFrameCount();
    v.bungeeInputBuf.resize ((size_t) maxIn * 2);
}

void VoicePool::startVoice (int voiceIdx, int sliceIdx, float velocity, int note,
                            SliceManager& sm,
                            float globalBpm, float globalPitch, int globalAlgorithm,
                            float globalAttack, float globalDecay, float globalSustain, float globalRelease,
                            int globalMuteGroup, bool globalPingPong,
                            bool globalStretchEnabled, float dawBpmVal,
                            float globalTonality, float globalFormant, bool globalFormantComp,
                            int globalGrainMode, float globalVolume,
                            const SampleData& sample)
{
    auto& v = voices[voiceIdx];
    const auto& s = sm.getSlice (sliceIdx);

    v.active    = true;
    v.sliceIdx  = sliceIdx;
    v.midiNote  = note;
    v.velocity  = velocity / 127.0f;
    v.position  = s.startSample;
    v.direction = 1;
    v.age       = 0;
    v.looping   = false;

    v.startSample = s.startSample;
    v.endSample   = s.endSample;

    // Resolve parameters via inheritance
    float attack   = sm.resolveParam (sliceIdx, kLockAttack,    s.attackSec,      globalAttack);
    float decay    = sm.resolveParam (sliceIdx, kLockDecay,     s.decaySec,       globalDecay);
    float sustain  = sm.resolveParam (sliceIdx, kLockSustain,   s.sustainLevel,   globalSustain);
    float release  = sm.resolveParam (sliceIdx, kLockRelease,   s.releaseSec,     globalRelease);

    v.envelope.noteOn (attack, decay, sustain, release);

    v.pingPong   = sm.resolveParam (sliceIdx, kLockPingPong,  s.pingPong ? 1.0f : 0.0f, globalPingPong ? 1.0f : 0.0f) > 0.5f;
    v.muteGroup  = (int) sm.resolveParam (sliceIdx, kLockMuteGroup, (float) s.muteGroup, (float) globalMuteGroup);

    int algo = (int) sm.resolveParam (sliceIdx, kLockAlgorithm, (float) s.algorithm, (float) globalAlgorithm);

    float sliceBpm = sm.resolveParam (sliceIdx, kLockBpm, s.bpm, globalBpm);
    float pitch = sm.resolveParam (sliceIdx, kLockPitch, s.pitchSemitones, globalPitch);
    v.pitchRatio = std::pow (2.0f, pitch / 12.0f);

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

    v.volume = sm.resolveParam (sliceIdx, kLockVolume, s.volume, globalVolume);

    // Reset stretch state
    v.stretchActive = false;
    v.wsolaActive = false;
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
            v.bungeeSpeed = (double) speedRatio;
            v.bungeeSrcPos = s.startSample;

            initBungee (v, pitch, sampleRate, hopAdj);
        }
        else
        {
            // Signalsmith Stretch: independent pitch + time
            v.stretchActive = true;
            v.speed = 1.0;
            v.stretchTimeRatio = speedRatio;
            v.stretchPitchSemis = pitch;
            v.stretchSrcPos = s.startSample;

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
            v.stretchSrcPos = s.startSample;

            initStretcher (v, pitch, sampleRate, tonality, formant, fComp, sample);
        }
        else if (algo == 2)
        {
            // Bungee algo but no stretch — use Bungee for pitch only
            v.bungeeActive = true;
            v.speed = 1.0;
            v.bungeeSpeed = 1.0;
            v.bungeeSrcPos = s.startSample;

            initBungee (v, pitch, sampleRate, hopAdj);
        }
        else
        {
            // Repitch: direct playback
            v.speed = v.pitchRatio;
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
        v.stretchInBufL[(size_t) i] = sample.getInterpolatedSample (pos, 0);
        v.stretchInBufR[(size_t) i] = sample.getInterpolatedSample (pos, 1);
        v.stretchSrcPos += 1.0;

        // Check bounds
        if (v.stretchSrcPos >= v.endSample)
        {
            if (v.pingPong)
            {
                v.stretchSrcPos = v.endSample - 1;
                // For simplicity, just clamp — ping-pong with stretch is edge case
            }
            else
            {
                // Fill rest with last sample
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

    auto& stretcher = *v.bungeeStretcher;

    // Set up the request for this grain
    Bungee::Request request;
    request.position = v.bungeeSrcPos;
    request.speed = v.bungeeSpeed;
    request.pitch = v.bungeePitch;
    request.reset = (v.bungeeOutAvail == 0 && v.bungeeOutReadPos == 0);
    request.resampleMode = resampleMode_autoOut;

    if (request.reset)
        stretcher.preroll (request);

    // Process grains until we have output
    stretcher.next (request);

    Bungee::InputChunk inputChunk = stretcher.specifyGrain (request);

    int numFrames = inputChunk.end - inputChunk.begin;
    int maxIn = stretcher.maxInputFrameCount();
    v.bungeeInputBuf.resize ((size_t) maxIn * 2);

    // Fill input buffer (non-interleaved: ch0 then ch1)
    for (int i = 0; i < numFrames; ++i)
    {
        double pos = inputChunk.begin + i;

        float sL = 0.0f, sR = 0.0f;
        if (pos >= v.startSample && pos < v.endSample)
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
    if (inputChunk.end > v.endSample)
        muteTail = inputChunk.end - v.endSample;

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
}

void VoicePool::processSample (const SampleData& sample, double sampleRate,
                               float& outL, float& outR)
{
    outL = 0.0f;
    outR = 0.0f;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[i];
        if (! v.active)
            continue;

        float voiceL = 0.0f, voiceR = 0.0f;

        if (v.stretchActive)
        {
            // Signalsmith Stretch processing
            float env = v.envelope.processSample (sampleRate);

            if (v.envelope.isDone())
            {
                v.active = false;
                v.stretcher.reset();
                voicePositions[i].store (0.0f, std::memory_order_relaxed);
                continue;
            }

            // Fill output buffer if empty
            if (v.stretchOutReadPos >= v.stretchOutAvail)
            {
                if (v.stretchSrcPos >= v.endSample && !v.pingPong)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
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
            float env = v.envelope.processSample (sampleRate);

            if (v.envelope.isDone())
            {
                v.active = false;
                v.bungeeStretcher.reset();
                voicePositions[i].store (0.0f, std::memory_order_relaxed);
                continue;
            }

            // Fill output buffer if empty
            if (v.bungeeOutReadPos >= v.bungeeOutAvail)
            {
                if (v.bungeeSrcPos >= v.endSample && !v.pingPong)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
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
                v.bungeeOutReadPos++;
            }

            v.age++;
            voicePositions[i].store ((float) v.bungeeSrcPos, std::memory_order_relaxed);
        }
        else if (v.wsolaActive)
        {
            GrainEngine::processVoice (v, sample, sampleRate, voiceL, voiceR);
            voicePositions[i].store ((float) v.position, std::memory_order_relaxed);
        }
        else
        {
            // Process envelope
            float env = v.envelope.processSample (sampleRate);

            if (v.envelope.isDone())
            {
                v.active = false;
                voicePositions[i].store (0.0f, std::memory_order_relaxed);
                continue;
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
                        // Wrap around for looping voices (e.g. lazy chop preview)
                        double len = (double) (v.endSample - v.startSample);
                        if (len > 0)
                            newPos = v.startSample + std::fmod (newPos - v.startSample, len);
                        if (newPos < v.startSample)
                            newPos += len;
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

        outL += voiceL;
        outR += voiceR;
    }
}
