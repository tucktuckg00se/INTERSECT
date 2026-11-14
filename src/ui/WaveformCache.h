#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../audio/SampleData.h"
#include "TimelineMap.h"
#include <functional>
#include <vector>

/** Min/max peaks per pixel for one row of the waveform view. */
class WaveformCache
{
public:
    struct Peak { float maxVal = 0.0f; float minVal = 0.0f; };

    /** The kit frame drawn at a timeline frame, or -1 where this row shows nothing (a member lane
        outside its group's slot). */
    using FrameMap = std::function<int (int timelineFrame)>;

    void rebuild (const juce::AudioBuffer<float>& buffer,
                  const std::array<SampleData::PeakMipmap, SampleData::kNumMipmapLevels>& mipmaps,
                  const TimelineMap::View& view, const FrameMap& toKit);

    /** Draws the peaks centred in `lane`, in runs where the row shows something: a filled shape
        when zoomed out, a line (and dots past 8 pixels per frame) when zoomed in. */
    void paint (juce::Graphics& g, juce::Rectangle<int> lane, juce::Colour colour,
                const TimelineMap::View& view, float verticalScale) const;

    const std::vector<Peak>& getPeaks() const { return peaks; }
    int getNumPeaks() const { return (int) peaks.size(); }
    /** False for pixels where the row shows nothing. */
    bool hasPeakAt (int px) const { return px >= 0 && px < (int) present.size() && present[(size_t) px]; }

private:
    std::vector<Peak> peaks;
    std::vector<bool> present;
};
