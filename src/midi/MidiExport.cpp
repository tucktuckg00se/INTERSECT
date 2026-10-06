#include "MidiExport.h"
#include "../PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace intersectMidi
{
namespace
{
constexpr int kTicksPerQuarter = 960;

float dbToLinear (float dB)
{
    if (dB <= -100.0f) return 0.0f;
    return std::pow (10.0f, dB / 20.0f);
}

void writeVarQuantity (std::vector<uint8_t>& out, uint32_t value)
{
    uint8_t buffer[5] {};
    int len = 1;
    buffer[0] = static_cast<uint8_t> (value & 0x7f);

    for (int i = 1; i < 5; ++i)
    {
        value >>= 7;
        if (value == 0)
            break;
        buffer[i] = static_cast<uint8_t> ((value & 0x7f) | 0x80);
        ++len;
    }

    for (int i = len - 1; i >= 0; --i)
        out.push_back (buffer[(size_t) i]);
}

void writeUInt32BE (std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back (static_cast<uint8_t> (v >> 24));
    out.push_back (static_cast<uint8_t> (v >> 16));
    out.push_back (static_cast<uint8_t> (v >> 8));
    out.push_back (static_cast<uint8_t> (v));
}

void writeUInt16BE (std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back (static_cast<uint8_t> (v >> 8));
    out.push_back (static_cast<uint8_t> (v));
}

struct NoteEvent
{
    int startTick = 0;
    int endTick = 0;
    int note = 0;
    int velocity = 100;
};

struct MidiEvent
{
    int tick = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

} // namespace

juce::MemoryBlock buildKitMidiFile (const IntersectProcessor::UiSliceSnapshot& ui, float globalBpm)
{
    juce::MemoryBlock out;

    if (ui.numSlices <= 0 || ui.sampleSampleRate <= 0.0)
        return out;

    // Kit BPM: the first active slice's resolved tempo (locked slices keep their own).
    float bpm = globalBpm > 0.0f ? globalBpm : 120.0f;
    for (int i = 0; i < ui.numSlices; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active)
            continue;
        bpm = (s.lockMask & kLockBpm) != 0 ? s.bpm : bpm;
        break;
    }
    bpm = juce::jlimit (20.0f, 300.0f, bpm > 0.0f ? bpm : 120.0f);

    const double ticksPerSecond = (double) kTicksPerQuarter * bpm / 60.0;
    const double sampleRate = ui.sampleSampleRate;

    std::vector<NoteEvent> notes;
    for (int i = 0; i < ui.numSlices; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        if (! s.active || s.endSample <= s.startSample)
            continue;

        NoteEvent e;
        e.startTick = (int) std::llround ((double) s.startSample / sampleRate * ticksPerSecond);
        e.endTick   = juce::jmax (e.startTick + 1,
                                  (int) std::llround ((double) s.endSample / sampleRate * ticksPerSecond));
        e.note      = juce::jlimit (0, kMidiNoteCount - 1, s.midiNote);
        // Slice volume (dB) shapes the velocity; keep a floor so quiet slices stay playable.
        e.velocity  = juce::jlimit (1, 127, juce::roundToInt (127.0f * juce::jlimit (0.0f, 1.0f, dbToLinear (s.volume))));
        notes.push_back (e);
    }

    if (notes.empty())
        return out;

    // Time order; note-offs before note-ons within the same tick.
    std::stable_sort (notes.begin(), notes.end(),
                      [] (const NoteEvent& a, const NoteEvent& b)
                      {
                          if (a.startTick != b.startTick)
                              return a.startTick < b.startTick;
                          return a.endTick < b.endTick;
                      });

    // Tempo first, at tick 0.
    std::vector<uint8_t> track;
    const uint32_t usPerQuarter = (uint32_t) std::llround (60'000'000.0 / bpm);
    writeVarQuantity (track, 0);
    track.push_back (0xFF);
    track.push_back (0x51);
    track.push_back (0x03);
    track.push_back ((uint8_t) (usPerQuarter >> 16));
    track.push_back ((uint8_t) (usPerQuarter >> 8));
    track.push_back ((uint8_t) usPerQuarter);

    // Expand to an event list and sort by tick so overlapping notes stay in order.
    std::vector<MidiEvent> events;
    events.reserve (notes.size() * 2);
    for (const auto& e : notes)
    {
        events.push_back ({ e.startTick, 0x90, (uint8_t) e.note, (uint8_t) e.velocity });
        events.push_back ({ e.endTick,   0x80, (uint8_t) e.note, 0x00 });
    }
    std::stable_sort (events.begin(), events.end(),
                      [] (const MidiEvent& a, const MidiEvent& b)
                      {
                          if (a.tick != b.tick)
                              return a.tick < b.tick;
                          return a.status < b.status;   // note-offs before note-ons on the same tick
                      });

    int lastTick = 0;
    for (const auto& ev : events)
    {
        writeVarQuantity (track, (uint32_t) (ev.tick - lastTick));
        track.push_back (ev.status);
        track.push_back (ev.data1);
        track.push_back (ev.data2);
        lastTick = ev.tick;
    }

    std::vector<uint8_t> file;
    file.reserve (track.size() + 14);
    file.push_back ('M'); file.push_back ('T'); file.push_back ('h'); file.push_back ('d');
    writeUInt32BE (file, 6);
    writeUInt16BE (file, 0);    // format 0
    writeUInt16BE (file, 1);    // one track
    writeUInt16BE (file, (uint16_t) kTicksPerQuarter);
    file.push_back ('M'); file.push_back ('T'); file.push_back ('r'); file.push_back ('k');
    writeUInt32BE (file, (uint32_t) track.size());
    file.insert (file.end(), track.begin(), track.end());

    out.setSize (file.size());
    std::copy (file.begin(), file.end(), static_cast<uint8_t*> (out.getData()));
    return out;
}

bool writeKitMidiFile (const juce::File& dest, const IntersectProcessor::UiSliceSnapshot& ui, float globalBpm)
{
    const auto data = buildKitMidiFile (ui, globalBpm);
    if (data.getSize() == 0)
        return false;

    return dest.replaceWithData (data.getData(), (int) data.getSize());
}

} // namespace intersectMidi
