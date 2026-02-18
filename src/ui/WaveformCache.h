#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../audio/SampleData.h"
#include <vector>

class WaveformCache
{
public:
    struct Peak { float maxVal = 0.0f; float minVal = 0.0f; };

    void rebuild (const juce::AudioBuffer<float>& buffer,
                  const std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels>& mipmaps,
                  int numFrames, float zoom, float scroll, int widthPixels);

    const std::vector<Peak>& getPeaks() const { return peaks; }
    int getNumPeaks() const { return (int) peaks.size(); }

private:
    std::vector<Peak> peaks;
};
