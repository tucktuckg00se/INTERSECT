#pragma once
#include "AdsrEnvelope.h"
#include <memory>
#include <vector>

// Forward declare to avoid including heavy header in every TU
namespace signalsmith { namespace stretch {
    template<typename Sample, class RandomEngine> struct SignalsmithStretch;
}}

namespace Bungee {
    struct Basic;
    template<class Edition> struct Stretcher;
}

struct Voice
{
    bool         active       = false;
    int          sliceIdx     = -1;
    double       position     = 0.0;
    double       speed        = 1.0;
    int          direction    = 1;       // 1=forward, -1=reverse
    int          midiNote     = -1;
    float        velocity     = 0.0f;
    AdsrEnvelope envelope;
    int          startSample  = 0;
    int          endSample    = 0;
    bool         pingPong     = false;
    int          muteGroup    = 0;
    int          age          = 0;
    bool         looping      = false;
    float        volume       = 1.0f;

    // WSOLA fields (legacy, still used for basic WSOLA fallback)
    bool         wsolaActive  = false;
    float        pitchRatio   = 1.0f;
    float        timeRatio    = 1.0f;
    double       wsolaSrcPos  = 0.0;
    double       wsolaPhase   = 0.0;

    // Signalsmith stretch fields
    bool         stretchActive = false;
    std::shared_ptr<signalsmith::stretch::SignalsmithStretch<float, void>> stretcher;
    std::vector<float> stretchInBufL, stretchInBufR;
    std::vector<float> stretchOutBufL, stretchOutBufR;
    int          stretchOutReadPos  = 0;
    int          stretchOutAvail    = 0;
    double       stretchSrcPos      = 0.0;
    float        stretchTimeRatio   = 1.0f;
    float        stretchPitchSemis  = 0.0f;

    // Bungee stretch fields
    bool         bungeeActive       = false;
    std::shared_ptr<Bungee::Stretcher<Bungee::Basic>> bungeeStretcher;
    std::vector<float> bungeeInputBuf;   // interleaved per-channel input buffer
    std::vector<float> bungeeOutBufL, bungeeOutBufR;
    int          bungeeOutReadPos   = 0;
    int          bungeeOutAvail     = 0;
    double       bungeeSrcPos       = 0.0;
    double       bungeePitch        = 1.0;
    double       bungeeSpeed        = 1.0;
};
