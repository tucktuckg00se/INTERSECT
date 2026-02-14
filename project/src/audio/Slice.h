#pragma once
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
    kLockPingPong  = 256,
    kLockStretch   = 512
};

struct Slice
{
    bool     active        = false;
    int      startSample   = 0;
    int      endSample     = 0;
    int      midiNote      = 36;
    float    bpm           = 120.0f;
    float    pitchSemitones = 0.0f;
    int      algorithm     = 0;       // 0=Direct, 1=WSOLA
    float    attackSec     = 0.005f;
    float    decaySec      = 0.1f;
    float    sustainLevel  = 1.0f;
    float    releaseSec    = 0.02f;
    int      muteGroup     = 1;
    bool     pingPong      = false;
    bool     stretchEnabled = false;
    uint32_t lockMask      = 0;
    juce::Colour colour    { 0.4f, 0.7f, 0.95f, 1.0f };
};
