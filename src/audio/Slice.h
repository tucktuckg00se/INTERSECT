#pragma once
#include "../Constants.h"
#include <cstdint>
#include <juce_graphics/juce_graphics.h>

enum LockBit : uint64_t
{
    kLockBpm       = 1,
    kLockPitch     = 2,
    kLockAlgorithm = 4,
    kLockAttack    = 8,
    kLockDecay     = 16,
    kLockSustain   = 32,
    kLockRelease   = 64,
    kLockMuteGroup = 128,
    kLockFilterEnabled = 256,
    kLockStretch       = 512,
    kLockTonality      = 1024,
    kLockFormant       = 2048,
    kLockFormantComp   = 4096,
    kLockGrainMode     = 8192,
    kLockVolume        = 16384,
    kLockReleaseTail   = 32768,
    kLockReverse       = 65536,
    kLockOutputBus     = 131072,
    kLockLoop          = 262144,
    kLockOneShot       = 524288,   // bit 19
    kLockCentsDetune   = 1048576,  // bit 20
    kLockFilterType       = 2097152u,
    kLockFilterSlope      = 4194304u,
    kLockFilterCutoff     = 8388608u,
    kLockFilterReso       = 16777216u,
    kLockFilterDrive      = 33554432u,
    kLockFilterKeyTrack   = 67108864u,
    kLockFilterEnvAttack  = 134217728u,
    kLockFilterEnvDecay   = 268435456u,
    kLockFilterEnvSustain = 536870912u,
    kLockFilterEnvRelease = 1073741824u,
    kLockFilterEnvAmount  = 0x80000000u,
    kLockFilterAsym       = 0x100000000ull,
    kLockCrossfade        = 0x200000000ull,
    kLockRepitchMode      = 0x400000000ull,
    kLockLoopStart        = 0x800000000ull,
    kLockLoopLength       = 0x1000000000ull,
    kLockHighNote         = 0x2000000000ull,   // bit 37
    kLockSliceRootNote    = 0x4000000000ull    // bit 38
};

inline int getMaxCrossfadeLengthSamples (int sliceLen, bool /*pingPong*/)
{
    return juce::jmax (0, sliceLen);
}

inline int crossfadePercentToSamples (float crossfadePct, int sliceLen, bool pingPong)
{
    const int maxFadeLen = getMaxCrossfadeLengthSamples (sliceLen, pingPong);
    if (crossfadePct <= 0.0f || maxFadeLen <= 0)
        return 0;

    const float normalised = juce::jlimit (0.0f, 100.0f, crossfadePct) / 100.0f;
    return juce::jlimit (1, maxFadeLen,
                         juce::roundToInt (normalised * (float) maxFadeLen));
}

inline int clampLoopCrossfadeLengthSamples (int fadeLen,
                                            int startSample,
                                            int endSample,
                                            int bufferEnd,
                                            bool reverse)
{
    const int preStartAvail = juce::jmax (0, startSample);
    const int postEndAvail  = juce::jmax (0, bufferEnd - endSample);
    return reverse ? juce::jmin (fadeLen, postEndAvail)
                   : juce::jmin (fadeLen, preStartAvail);
}

inline int clampPingPongCrossfadeLengthSamples (int fadeLen,
                                                int startSample,
                                                int endSample,
                                                int bufferEnd)
{
    const int preStartAvail = juce::jmax (0, startSample);
    const int postEndAvail  = juce::jmax (0, bufferEnd - endSample);
    return juce::jmin (fadeLen, juce::jmin (preStartAvail, postEndAvail));
}

struct Slice
{
    bool     active        = false;
    int      startSample   = 0;
    int      endSample     = 0;
    int      midiNote      = kDefaultRootNote;
    int      highNote      = kDefaultRootNote;    // high end of note range
    int      sliceRootNote = kDefaultRootNote;    // root note for pitch transpose
    float    bpm           = 120.0f;
    float    pitchSemitones = 0.0f;
    int      algorithm     = 0;       // 0=Repitch, 1=Stretch, 2=Bungee
    int      repitchMode   = (int) RepitchMode::Linear;
    float    attackSec     = 0.005f;
    float    decaySec      = 0.1f;
    float    sustainLevel  = 1.0f;
    float    releaseSec    = 0.02f;
    int      muteGroup     = 1;
    int      loopMode      = 0;       // 0=Off, 1=Loop, 2=Ping-Pong
    bool     stretchEnabled = false;
    float    tonalityHz    = 0.0f;
    float    formantSemitones = 0.0f;
    bool     formantComp   = false;
    int      grainMode     = 0;       // Bungee: -1=Fast, 0=Normal, +1=Smooth
    float    volume        = 0.0f;
    bool     releaseTail   = false;
    bool     reverse       = false;
    int      outputBus     = 0;
    bool     oneShot       = false;
    float    centsDetune   = 0.0f;      // fine pitch: -100..+100 cents
    bool     filterEnabled = false;
    int      filterType    = 0;         // 0=LP, 1=HP, 2=BP, 3=Notch
    int      filterSlope   = 0;         // 0=12dB, 1=24dB
    float    filterCutoff  = 8200.0f;
    float    filterReso    = 0.0f;
    float    filterDrive   = 0.0f;
    float    filterAsym    = 0.0f;
    float    filterKeyTrack = 0.0f;
    float    filterEnvAttackSec  = 0.0f;
    float    filterEnvDecaySec   = 0.0f;
    float    filterEnvSustain    = 1.0f;
    float    filterEnvReleaseSec = 0.0f;
    float    filterEnvAmount     = 0.0f; // semitones bipolar
    float    crossfadePct       = 0.0f; // 0-100, percentage of the mode-dependent fade range
    int      loopStartOffset   = 0;    // samples from slice start (0 = slice start)
    int      loopLength        = 0;    // samples (0 = full slice length)
    uint64_t lockMask      = 0;
    juce::Colour colour    { 0.4f, 0.7f, 0.95f, 1.0f };
};
