#include "MidiExport.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace intersectMidi
{
namespace
{
constexpr int kTicksPerQuarter = 960;
constexpr int kDefaultVelocity = 100;

// Maps a frame offset within the sample to quarter-note beats.
class BeatMap
{
public:
    BeatMap (const SampleData::SessionSample* sample, double sampleRate, float fallbackBpm)
        : rate (sampleRate)
    {
        if (sample != nullptr && ! sample->beatAnchors.empty() && sample->anchorTempoBpm > 0.0f)
        {
            anchors = &sample->beatAnchors;
            bpm = sample->anchorTempoBpm;
        }
        else
        {
            bpm = fallbackBpm;
        }
    }

    float getBpm() const { return bpm; }

    double toBeats (int frame) const
    {
        const double beatsPerFrame = (double) bpm / (60.0 * rate);
        if (anchors == nullptr)
            return frame * beatsPerFrame;

        // Piecewise-linear between REX slice starts: each rendered slice (including its
        // transient tail) spans exactly the gap to the next slice's musical position.
        const auto& a = *anchors;
        auto next = std::upper_bound (a.begin(), a.end(), frame,
                                      [] (int f, const SampleData::SessionSample::BeatAnchor& anchor)
                                      { return f < anchor.frame; });
        if (next == a.begin())
            return a.front().beat - (a.front().frame - frame) * beatsPerFrame;

        const auto& prev = *(next - 1);
        if (next == a.end() || next->frame <= prev.frame)
            return prev.beat + (frame - prev.frame) * beatsPerFrame;

        const double t = (double) (frame - prev.frame) / (double) (next->frame - prev.frame);
        return prev.beat + t * (next->beat - prev.beat);
    }

private:
    const std::vector<SampleData::SessionSample::BeatAnchor>* anchors = nullptr;
    double rate = 44100.0;
    float bpm = 120.0f;
};

int velocityFor (const Slice& s)
{
    if ((s.lockMask & kLockVolume) == 0)
        return kDefaultVelocity;

    const float gain = s.volume <= -100.0f ? 0.0f : juce::Decibels::decibelsToGain (s.volume);
    return juce::jlimit (1, 127, juce::roundToInt (127.0f * juce::jlimit (0.0f, 1.0f, gain)));
}

// Tempo for non-REX samples: the first active slice's locked BPM, else the sample's own BPM.
float sampleTempo (const IntersectProcessor::UiSliceSnapshot& ui, int sampleId, float sampleBpm)
{
    for (int i = 0; i < ui.numSlices; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active || (sampleId >= 0 && s.sampleId != sampleId))
            continue;
        if ((s.lockMask & kLockBpm) != 0 && s.bpm > 0.0f)
            return s.bpm;
        break;
    }
    return sampleBpm;
}
} // namespace

juce::MemoryBlock buildKitMidiFile (const IntersectProcessor::UiSliceSnapshot& ui,
                                    const SampleData::SessionSample* sample,
                                    float sampleBpm)
{
    if (ui.numSlices <= 0 || ui.sampleSampleRate <= 0.0)
        return {};

    const int sampleId = sample != nullptr ? sample->sampleId : -1;
    const float fallbackBpm = juce::jlimit (20.0f, 999.0f, sampleTempo (ui, sampleId, sampleBpm > 0.0f ? sampleBpm : 120.0f));
    const BeatMap beats (sample, ui.sampleSampleRate, fallbackBpm);

    struct Note { int on, off, note, velocity; };
    std::vector<Note> notes;
    for (int i = 0; i < ui.numSlices; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active || (sample != nullptr && s.sampleId != sampleId))
            continue;

        const int start = sample != nullptr ? s.startInSample : s.startSample;
        const int end   = sample != nullptr ? s.endInSample   : s.endSample;
        if (end <= start)
            continue;

        const int on  = juce::jmax (0, (int) std::llround (beats.toBeats (start) * kTicksPerQuarter));
        const int off = juce::jmax (on + 1, (int) std::llround (beats.toBeats (end) * kTicksPerQuarter));
        notes.push_back ({ on, off, juce::jlimit (0, kMidiNoteCount - 1, s.midiNote), velocityFor (s) });
    }

    if (notes.empty())
        return {};

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / beats.getBpm())), 0.0);

    // Note-offs first: addEvent() places an event after others with the same timestamp, so a
    // note ending exactly where the next one starts is released before it retriggers.
    for (const auto& n : notes)
        track.addEvent (juce::MidiMessage::noteOff (1, n.note), (double) n.off);
    for (const auto& n : notes)
        track.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) n.velocity), (double) n.on);
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kTicksPerQuarter);
    file.addTrack (track);

    juce::MemoryOutputStream out;
    if (! file.writeTo (out, 0))
        return {};
    return out.getMemoryBlock();
}

bool writeKitMidiFile (const juce::File& dest,
                       const IntersectProcessor::UiSliceSnapshot& ui,
                       const SampleData::SessionSample* sample,
                       float sampleBpm)
{
    const auto data = buildKitMidiFile (ui, sample, sampleBpm);
    if (data.getSize() == 0)
        return false;

    return dest.replaceWithData (data.getData(), data.getSize());
}

} // namespace intersectMidi
