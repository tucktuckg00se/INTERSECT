#pragma once
#include "../Constants.h"
#include <cstdint>
#include <juce_graphics/juce_graphics.h>

enum LockBit : uint32_t
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
    kLockFilterEnvAmount  = 0x80000000u
};

struct Slice
{
    bool     active        = false;
    int      startSample   = 0;
    int      endSample     = 0;
    int      midiNote      = kDefaultRootNote;
    float    bpm           = 120.0f;
    float    pitchSemitones = 0.0f;
    int      algorithm     = 0;       // 0=Repitch, 1=Stretch, 2=Bungee
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
    float    filterKeyTrack = 0.0f;
    float    filterEnvAttackSec  = 0.0f;
    float    filterEnvDecaySec   = 0.0f;
    float    filterEnvSustain    = 1.0f;
    float    filterEnvReleaseSec = 0.0f;
    float    filterEnvAmount     = 0.0f; // semitones bipolar
    uint32_t lockMask      = 0;
    juce::Colour colour    { 0.4f, 0.7f, 0.95f, 1.0f };
};
