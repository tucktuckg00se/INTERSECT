// Processor-level tests: the real IntersectProcessor, driven the way a host and the editor drive it.
//
//   IntersectProcessorTests state    save/restore round trip, every saved-state fixture, kit edits while playing
//   IntersectProcessorTests stress   restores (preset/project loads) while audio is rendering
//
// The main thread is the message thread. Fixtures live in tests/fixtures (see extract_state.py).

#include "PluginProcessor.h"
#include "audio/SliceEdits.h"
#include "state/SessionState.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

juce::File writeSine (const juce::File& file, int numFrames)
{
    juce::AudioBuffer<float> buffer (2, numFrames);
    for (int i = 0; i < numFrames; ++i)
    {
        const auto value = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * i / 109.0);
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (kRate).withNumChannels (2).withBitsPerSample (16);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    require (writer != nullptr, "could not write test wav");
    writer->writeFromAudioSampleBuffer (buffer, 0, numFrames);
    return file;
}

// One second at each level, so the output shows which half (slice) is playing.
juce::File writeTwoLevels (const juce::File& file, float firstLevel, float secondLevel)
{
    const int half = (int) kRate;
    juce::AudioBuffer<float> buffer (2, half * 2);
    for (int i = 0; i < half * 2; ++i)
    {
        const auto level = i < half ? firstLevel : secondLevel;
        const auto value = level * (float) std::sin (juce::MathConstants<double>::twoPi * i / 109.0);
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate (kRate).withNumChannels (2).withBitsPerSample (16);
    auto writer = juce::WavAudioFormat().createWriterFor (stream, options);
    require (writer != nullptr, "could not write test wav");
    writer->writeFromAudioSampleBuffer (buffer, 0, half * 2);
    return file;
}

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("intersect-processor-test", {});
    TempDir()  { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

// A processor plus what a host does around it: prepare, render blocks, run the message thread.
struct Host
{
    IntersectProcessor processor;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;

    Host()
    {
        processor.setRateAndBufferSizeDetails (kRate, kBlock);
        processor.prepareToPlay (kRate, kBlock);
        buffer.setSize (processor.getTotalNumOutputChannels(), kBlock);
    }

    ~Host() { processor.releaseResources(); }

    void renderBlock()
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();
    }

    // Render blocks and run the message loop until done() holds (sample loads land in processBlock).
    template <typename Done>
    void runUntil (Done done, const std::string& what, juce::uint32 timeoutMs = 10000)
    {
        const auto start = juce::Time::getMillisecondCounter();
        while (! done())
        {
            renderBlock();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            require (juce::Time::getMillisecondCounter() - start < timeoutMs, "timed out waiting for " + what);
        }
    }

    const IntersectProcessor::UiSliceSnapshot& ui() const { return processor.getUiSliceSnapshot(); }

    void push (IntersectProcessor::CommandType type, int a = 0, int b = 0, float value = 0.0f)
    {
        IntersectProcessor::Command cmd;
        cmd.type = type;
        cmd.intParam1 = a;
        cmd.intParam2 = b;
        cmd.floatParam1 = value;
        processor.pushCommand (cmd);
    }

    void setSliceParam (int slice, int field, float value)
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSetSliceParam;
        cmd.sliceIdx = slice;
        cmd.intParam1 = field;
        cmd.floatParam1 = value;
        processor.pushCommand (cmd);
    }

    void toggleLock (int slice, uint64_t lockBit)
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdToggleLock;
        cmd.sliceIdx = slice;
        cmd.lockBitParam = lockBit;
        processor.pushCommand (cmd);
    }

    juce::MemoryBlock save()
    {
        juce::MemoryBlock block;
        processor.getStateInformation (block);
        return block;
    }

    void restore (const juce::MemoryBlock& block)
    {
        processor.setStateInformation (block.getData(), (int) block.getSize());
    }

    // Loads one generated file and adds slices; returns the session sample id.
    int loadKit (const juce::File& wav, int numSlices)
    {
        processor.loadFilesAsync ({ wav }, false);
        runUntil ([&] { return ui().sampleLoaded && ui().numSessionSamples == 1; }, "the sample to load");

        const int length = ui().sampleNumFrames;
        for (int i = 0; i < numSlices; ++i)
            push (IntersectProcessor::CmdCreateSlice, length * i / numSlices, length * (i + 1) / numSlices);
        runUntil ([&] { return ui().numSlices == numSlices; }, "the slices");
        return ui().sessionSamples[0].sampleId;
    }
};

float sampleBpm (const IntersectProcessor::UiSliceSnapshot& ui, int sampleId)
{
    return ui.sampleParamsFor (sampleId).getField (IntersectProcessor::FieldBpm);
}

// Saved states must match byte for byte; on a mismatch, say where they part.
void requireSameBytes (const juce::MemoryBlock& actual, const juce::MemoryBlock& expected, const std::string& what)
{
    if (actual == expected)
        return;

    const auto size = juce::jmin (actual.getSize(), expected.getSize());
    size_t at = 0;
    while (at < size && actual[at] == expected[at])
        ++at;
    if (const auto dumpDir = juce::SystemStats::getEnvironmentVariable ("INTERSECT_TEST_DUMP_DIR", {}); dumpDir.isNotEmpty())
    {
        juce::File (dumpDir).getChildFile ("actual.bin").replaceWithData (actual.getData(), actual.getSize());
        juce::File (dumpDir).getChildFile ("expected.bin").replaceWithData (expected.getData(), expected.getSize());
    }
    throw std::runtime_error (what + " (" + std::to_string (actual.getSize()) + " vs " + std::to_string (expected.getSize())
                              + " bytes, first difference at byte " + std::to_string (at) + ")");
}

// Characterisation: the exact bytes today's code saves pin the file format through refactors.
// INTERSECT_UPDATE_EXPECTED=1 rewrites tests/fixtures/expected/ instead of comparing; do that
// only on purpose (a deliberate format change), never to make a failing test pass.
void requireExpectedBytes (const juce::MemoryBlock& actual, const juce::String& expectedName, const std::string& what)
{
    const auto file = juce::File (INTERSECT_TEST_FIXTURES_DIR).getChildFile ("expected").getChildFile (expectedName);
    if (juce::SystemStats::getEnvironmentVariable ("INTERSECT_UPDATE_EXPECTED", {}) == "1")
    {
        file.getParentDirectory().createDirectory();
        require (file.replaceWithData (actual.getData(), actual.getSize()), "could not write " + expectedName.toStdString());
        return;
    }

    juce::MemoryBlock expected;
    require (file.loadFileAsData (expected), what + ": no " + expectedName.toStdString() + " (generate it with INTERSECT_UPDATE_EXPECTED=1)");
    requireSameBytes (actual, expected, what + " saves the bytes in expected/" + expectedName.toStdString());
}

// Replaces every copy of `from` (a temp folder path) in a saved state, so it compares across runs.
// JUCE strings are null-terminated with no length prefix, so the length may change.
juce::MemoryBlock replacePath (const juce::MemoryBlock& state, const juce::String& from, const juce::String& to)
{
    std::string bytes (static_cast<const char*> (state.getData()), state.getSize());
    const auto fromUtf8 = from.toStdString();
    const auto toUtf8 = to.toStdString();
    for (auto at = bytes.find (fromUtf8); at != std::string::npos; at = bytes.find (fromUtf8, at + toUtf8.size()))
        bytes.replace (at, fromUtf8.size(), toUtf8);
    return juce::MemoryBlock (bytes.data(), bytes.size());
}

bool wasRejected (const IntersectProcessor::UiSliceSnapshot& ui)
{
    return ui.hasStatusMessage && ui.statusMessage.toString().contains ("Unsupported");
}

void stateRoundTrips()
{
    TempDir temp;
    const auto wav = writeSine (temp.dir.getChildFile ("loop.wav"), (int) kRate * 2);

    Host a;
    const int sampleId = a.loadKit (wav, 2);
    a.push (IntersectProcessor::CmdSetSampleParam, sampleId, IntersectProcessor::FieldBpm, 128.0f);
    a.runUntil ([&] { return juce::exactlyEqual (sampleBpm (a.ui(), sampleId), 128.0f); }, "the BPM edit");
    const auto saved = a.save();

    Host b;
    b.restore (saved);
    b.runUntil ([&] { return b.ui().sampleLoaded && b.ui().numSessionSamples == 1 && b.ui().numSlices == 2; },
                "the restored kit");

    require (! wasRejected (b.ui()), "the state was rejected");
    require (b.ui().sessionSamples[0].sampleId == sampleId, "sample id kept");
    require (b.ui().sessionSamples[0].fileName.toString() == "loop.wav", "file name kept");
    require (juce::exactlyEqual (sampleBpm (b.ui(), sampleId), 128.0f), "sample BPM kept");
    for (int i = 0; i < 2; ++i)
    {
        const auto& before = a.ui().slices[(size_t) i];
        const auto& after = b.ui().slices[(size_t) i];
        require (after.startSample == before.startSample && after.endSample == before.endSample,
                 "slice " + std::to_string (i) + " bounds kept");
        require (after.midiNote == before.midiNote, "slice " + std::to_string (i) + " note kept");
        require (after.sampleId == before.sampleId, "slice " + std::to_string (i) + " owner kept");
    }
    requireSameBytes (b.save(), saved, "saving the restored kit gives the same bytes");
}

// Every fixture loads (its samples are missing, so they come back as placeholders), keeps its
// slices, and a re-save loads back to a kit that saves to the same bytes.
void fixturesLoad()
{
    const juce::File dir (INTERSECT_TEST_FIXTURES_DIR);
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.bin");
    require (! files.isEmpty(), "no fixtures in " + dir.getFullPathName().toStdString());
    files.sort();

    for (const auto& file : files)
    {
        const auto name = file.getFileName().toStdString();
        const auto info = juce::JSON::parse (file.withFileExtension ("json"));
        const int numSlices = juce::jmin ((int) info["numSlices"], SliceManager::kMaxSlices);
        const int numSamples = info["numSamples"];
        require (numSamples > 0, name + ": sidecar json missing or empty");

        juce::MemoryBlock blob;
        require (file.loadFileAsData (blob), name + ": could not read");

        const auto loaded = [numSlices, numSamples] (const Host& host)
        {
            return host.ui().numSessionSamples == numSamples && host.ui().numSlices == numSlices;
        };
        const auto waitFor = [&] (Host& host, const std::string& what)
        {
            try
            {
                host.runUntil ([&] { return loaded (host) || wasRejected (host.ui()); }, what);
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error (std::string (e.what()) + " (has " + std::to_string (host.ui().numSessionSamples)
                                          + " sample(s), " + std::to_string (host.ui().numSlices) + " slice(s); expected "
                                          + std::to_string (numSamples) + ", " + std::to_string (numSlices) + ")");
            }
        };

        Host host;
        host.restore (blob);
        waitFor (host, name + " to load");
        require (! wasRejected (host.ui()), name + " was rejected");
        require (host.ui().numMissingSessionSamples == numSamples, name + ": every sample is a placeholder");

        const auto resaved = host.save();
        requireExpectedBytes (resaved, file.getFileName(), name);
        Host again;
        again.restore (resaved);
        waitFor (again, name + " re-saved, to load");
        require (! wasRejected (again.ui()), name + " re-saved was rejected");
        requireSameBytes (again.save(), resaved, name + ": re-saving is stable");
        std::cout << "  " << name << ": " << numSamples << " sample(s), " << numSlices << " slice(s)\n";
    }
}

// A kit using most of the format: two real samples, slice overrides (with their locks),
// per-sample params, root note and selection. Saved bytes must match expected/rich-kit.bin.
void richKitSavesExpectedBytes()
{
    TempDir temp;
    const auto first = writeSine (temp.dir.getChildFile ("first.wav"), (int) kRate);
    const auto second = writeSine (temp.dir.getChildFile ("second.wav"), (int) kRate / 2);

    Host host;
    const int firstId = host.loadKit (first, 3);
    host.processor.addFilesToKit ({ second });
    host.runUntil ([&] { return host.ui().numSessionSamples == 2; }, "the second sample");
    const int secondId = host.ui().sessionSamples[1].sampleId;

    host.push (IntersectProcessor::CmdSetSampleParam, firstId, IntersectProcessor::FieldBpm, 96.0f);
    host.push (IntersectProcessor::CmdSetSampleParam, secondId, IntersectProcessor::FieldPitch, -5.0f);
    host.setSliceParam (0, IntersectProcessor::FieldPitch, 7.0f);
    host.setSliceParam (2, IntersectProcessor::FieldBpm, 140.0f);
    host.push (IntersectProcessor::CmdSetRootNote, 48);
    host.push (IntersectProcessor::CmdSelectSlice, 1);
    host.runUntil ([&]
    {
        const auto& ui = host.ui();
        return ui.rootNote == 48 && ui.selectedSlice == 1 && juce::exactlyEqual (ui.slices[2].bpm, 140.0f)
            && juce::exactlyEqual (sampleBpm (ui, firstId), 96.0f);
    }, "the edits");

    const auto folder = temp.dir.getFullPathName() + "/";
    requireExpectedBytes (replacePath (host.save(), folder, "/kit/"), "rich-kit.bin", "rich kit");
}

// Every slice field set through CmdSetSliceParam, out-of-range values (clamping), a value equal to
// the sample's own (which clears that field's lock) and lock toggles, pinned byte for byte by
// expected/slice-fields.bin. Guards the slice-edit code when it moves.
void sliceFieldEditsSaveExpectedBytes()
{
    TempDir temp;
    const auto wav = writeSine (temp.dir.getChildFile ("fields.wav"), (int) kRate * 2);

    Host host;
    host.loadKit (wav, 3);
    using P = IntersectProcessor;
    const std::vector<std::pair<int, float>> everyField {
        { P::FieldBpm, 140.0f }, { P::FieldPitch, 3.0f }, { P::FieldAlgorithm, 1.0f }, { P::FieldRepitchMode, 1.0f },
        { P::FieldAttack, 0.05f }, { P::FieldDecay, 0.3f }, { P::FieldSustain, 0.6f }, { P::FieldRelease, 0.4f },
        { P::FieldMuteGroup, 3.0f }, { P::FieldMidiNote, 50.0f }, { P::FieldStretchEnabled, 1.0f }, { P::FieldTonality, 400.0f },
        { P::FieldFormant, 2.0f }, { P::FieldFormantComp, 1.0f }, { P::FieldGrainMode, 1.0f }, { P::FieldVolume, -6.0f },
        { P::FieldReleaseTail, 1.0f }, { P::FieldReverse, 1.0f }, { P::FieldOutputBus, 2.0f }, { P::FieldLoop, 1.0f },
        { P::FieldOneShot, 1.0f }, { P::FieldCentsDetune, 12.0f }, { P::FieldFilterEnabled, 1.0f }, { P::FieldFilterType, 2.0f },
        { P::FieldFilterSlope, 1.0f }, { P::FieldFilterCutoff, 2000.0f }, { P::FieldFilterReso, 30.0f }, { P::FieldFilterDrive, 20.0f },
        { P::FieldFilterKeyTrack, 50.0f }, { P::FieldFilterEnvAttack, 0.1f }, { P::FieldFilterEnvDecay, 0.2f },
        { P::FieldFilterEnvSustain, 0.5f }, { P::FieldFilterEnvRelease, 0.3f }, { P::FieldFilterEnvAmount, 12.0f },
        { P::FieldFilterAsym, 25.0f }, { P::FieldCrossfade, 10.0f }, { P::FieldLoopStart, 500.0f }, { P::FieldLoopLength, 4000.0f },
        { P::FieldHighNote, 55.0f }, { P::FieldSliceRootNote, 52.0f },
    };
    int pushed = 0;
    const auto settle = [&host, &pushed]
    {
        if (++pushed % 8 == 0)
            host.renderBlock();   // keep well inside the command FIFO
    };

    for (const auto& [field, value] : everyField)
    {
        host.setSliceParam (0, field, value);
        settle();
    }

    // Slice 1: out-of-range values, then a value equal to the sample's own (unlocks that field).
    const float sampleBpm = host.ui().sampleParamsFor (host.ui().slices[1].sampleId).getField (P::FieldBpm);
    for (const auto& [field, value] : std::vector<std::pair<int, float>> {
             { P::FieldPitch, 99.0f }, { P::FieldVolume, 100.0f }, { P::FieldMuteGroup, 999.0f },
             { P::FieldFilterCutoff, 1.0e6f }, { P::FieldMidiNote, 200.0f }, { P::FieldBpm, 180.0f }, { P::FieldBpm, sampleBpm } })
    {
        host.setSliceParam (1, field, value);
        settle();
    }

    // Slice 2: locks switched on (copying the sample's value) and one switched on and off again.
    for (const uint64_t bit : { (uint64_t) kLockPitch, (uint64_t) kLockAttack, (uint64_t) kLockFilterCutoff,
                                (uint64_t) kLockVolume, (uint64_t) kLockVolume })
    {
        host.toggleLock (2, bit);
        settle();
    }

    int blocks = 0;
    host.runUntil ([&] { return ++blocks > 8; }, "the edits");
    require (host.ui().slices[0].crossfadePct > 9.0f && (host.ui().slices[2].lockMask & kLockPitch) != 0, "the edits landed");
    requireExpectedBytes (replacePath (host.save(), temp.dir.getFullPathName() + "/", "/kit/"), "slice-fields.bin", "slice field edits");
}

// The slice commands that create or reshape slices: set bounds, duplicate, split into equal parts
// and chop at transients. Pinned byte for byte by expected/slice-commands.bin.
void sliceCommandsSaveExpectedBytes()
{
    TempDir temp;
    const auto wav = writeSine (temp.dir.getChildFile ("commands.wav"), (int) kRate * 4);

    Host host;
    host.loadKit (wav, 3);   // 0-1.33 s, 1.33-2.67 s, 2.67-4 s
    const auto run = [&host] (IntersectProcessor::Command cmd, int expectedSlices, const char* what)
    {
        host.processor.pushCommand (cmd);
        host.runUntil ([&] { return host.ui().numSlices == expectedSlices; }, what);
    };
    const auto make = [] (IntersectProcessor::CommandType type, int slice)
    {
        IntersectProcessor::Command cmd;
        cmd.type = type;
        cmd.sliceIdx = slice;
        return cmd;
    };

    auto bounds = make (IntersectProcessor::CmdSetSliceBounds, -1);
    bounds.intParam1 = 0;
    bounds.intParam2 = 1000;
    bounds.positions[0] = 50000;
    bounds.numPositions = 1;
    host.processor.pushCommand (bounds);
    host.runUntil ([&] { return host.ui().slices[0].startSample == 1000 && host.ui().slices[0].endSample == 50000; }, "new bounds");

    run (make (IntersectProcessor::CmdDuplicateSlice, 0), 4, "the duplicate");
    auto split = make (IntersectProcessor::CmdSplitSlice, 1);
    split.intParam1 = 3;
    run (split, 6, "the split");
    auto chop = make (IntersectProcessor::CmdTransientChop, 0);
    chop.positions[0] = 10000;
    chop.positions[1] = 30000;
    chop.numPositions = 2;
    run (chop, 8, "the transient chop");

    requireExpectedBytes (replacePath (host.save(), temp.dir.getFullPathName() + "/", "/kit/"), "slice-commands.bin", "slice commands");
}

// SliceEdits on its own: values, lock bits (the sample's own value unlocks), clamping, and the
// MIDI-map rebuild flag for note fields.
void sliceEditsOnTheirOwn()
{
    using P = ParamFieldIds;
    const auto sample = SampleParams::factoryDefaults();
    Slice s;
    s.midiNote = s.highNote = s.sliceRootNote = 36;

    require (! SliceEdits::setField (s, sample, P::FieldPitch, 3.0f) && juce::exactlyEqual (s.pitchSemitones, 3.0f)
             && (s.lockMask & kLockPitch) != 0, "a different value sets the field and its lock");
    SliceEdits::setField (s, sample, P::FieldPitch, sample.pitchSemitones);
    require ((s.lockMask & kLockPitch) == 0, "the sample's own value clears the lock");
    SliceEdits::setField (s, sample, P::FieldBpm, sample.bpm + 5.0e-5f);
    require ((s.lockMask & kLockBpm) == 0, "within 1e-4 of the sample's value counts as equal");

    SliceEdits::setField (s, sample, P::FieldOutputBus, 99.0f);
    require (s.outputBus == kMaxOutputBuses - 1 && (s.lockMask & kLockOutputBus) != 0, "output bus is clamped and locked");
    SliceEdits::setField (s, sample, P::FieldFilterType, 9.0f);
    require (s.filterType == 3, "filter type is clamped");

    require (SliceEdits::setField (s, sample, P::FieldMidiNote, 50.0f), "a note change asks for a MIDI map rebuild");
    require (s.midiNote == 50 && s.highNote == 50 && s.sliceRootNote == 50, "a single-note slice moves as a whole");
    require (SliceEdits::setField (s, sample, P::FieldHighNote, 40.0f) && s.highNote == 50, "the high note can't go below the low note");
    SliceEdits::setField (s, sample, P::FieldHighNote, 60.0f);
    require (! SliceEdits::setField (s, sample, P::FieldSliceRootNote, 70.0f) && s.sliceRootNote == 60,
             "the root note stays inside the range and needs no rebuild");

    s.attackSec = 0.9f;
    SliceEdits::copySampleParam (s, sample, kLockAttack);
    require (juce::exactlyEqual (s.attackSec, sample.attackSec), "switching a lock on copies the sample's value");
}

// A damaged state is rejected before anything is applied, and says why. (It used to reset the
// root note and the slice count, then fail silently.)
void damagedStateChangesNothing()
{
    TempDir temp;
    const auto other = writeSine (temp.dir.getChildFile ("other.wav"), (int) kRate);
    const auto mine = writeSine (temp.dir.getChildFile ("mine.wav"), (int) kRate);

    Host source;
    source.loadKit (other, 3);
    auto damaged = source.save();
    damaged.setSize (damaged.getSize() / 2);   // cut off mid-way, as a broken project file would be

    Host host;
    host.loadKit (mine, 2);
    host.push (IntersectProcessor::CmdSetRootNote, 48);
    host.runUntil ([&] { return host.ui().rootNote == 48; }, "the root note");
    const auto before = host.save();

    host.restore (damaged);
    host.runUntil ([&] { return wasRejected (host.ui()) || host.ui().hasStatusMessage; }, "the restore to finish");
    for (int i = 0; i < 4; ++i)
        host.renderBlock();

    require (host.ui().hasStatusMessage && host.ui().statusMessage.toString().contains ("damaged"), "says the data is damaged");
    require (host.ui().numSlices == 2 && host.ui().rootNote == 48, "the kit is untouched");
    requireSameBytes (host.save(), before, "the kit saves exactly as before");
}

// A slice's loop survives saving and reopening a project whose sample is missing. The missing
// file comes back as a 2 s placeholder, so a slice past its end is parked at a stub of absolute
// bounds; the next load used to clamp the loop to that stub, losing it even after a relink.
void missingSampleKeepsLoopPoints()
{
    TempDir temp;
    const auto wav = writeSine (temp.dir.getChildFile ("long.wav"), (int) kRate * 5);

    Host host;
    host.processor.loadFilesAsync ({ wav }, false);
    host.runUntil ([&] { return host.ui().sampleLoaded; }, "the sample to load");
    host.push (IntersectProcessor::CmdCreateSlice, (int) kRate * 3, (int) kRate * 4);
    host.runUntil ([&] { return host.ui().numSlices == 1; }, "the slice");
    host.setSliceParam (0, IntersectProcessor::FieldLoopStart, 1000.0f);
    host.setSliceParam (0, IntersectProcessor::FieldLoopLength, 2000.0f);
    host.runUntil ([&] { return host.ui().slices[0].loopStartOffset == 1000 && host.ui().slices[0].loopLength == 2000; },
                   "the loop edit");
    const auto saved = host.save();
    require (wav.deleteFile(), "could not delete the sample");

    const auto reopen = [] (const juce::MemoryBlock& state, Host& into)
    {
        into.restore (state);
        into.runUntil ([&] { return into.ui().numMissingSessionSamples == 1 && into.ui().numSlices == 1; },
                       "the project with its missing sample");
    };
    Host missing;
    reopen (saved, missing);
    require (missing.ui().slices[0].parked, "the slice past the placeholder is parked");

    Host reopened;
    reopen (missing.save(), reopened);
    require (reopened.ui().slices[0].loopStartOffset == 1000 && reopened.ui().slices[0].loopLength == 2000,
             "the loop is kept (got " + std::to_string (reopened.ui().slices[0].loopStartOffset) + "+"
                 + std::to_string (reopened.ui().slices[0].loopLength) + ")");
}

// MIDI in -> audio out: a slice's note plays that slice, and note-off ends it after the release.
void notesPlayTheirSlices()
{
    TempDir temp;
    const auto wav = writeTwoLevels (temp.dir.getChildFile ("levels.wav"), 0.5f, 0.1f);

    Host host;
    host.loadKit (wav, 2);   // slice 0 = the loud second, slice 1 = the quiet one
    const int loudNote = host.ui().slices[0].midiNote;
    const int quietNote = host.ui().slices[1].midiNote;
    require (loudNote != quietNote, "the slices have their own notes");

    const auto rms = [&host] (int blocks)
    {
        double sum = 0.0;
        for (int b = 0; b < blocks; ++b)
        {
            host.renderBlock();
            for (int i = 0; i < host.buffer.getNumSamples(); ++i)
                sum += (double) host.buffer.getSample (0, i) * host.buffer.getSample (0, i);
        }
        return std::sqrt (sum / (blocks * host.buffer.getNumSamples()));
    };

    require (rms (8) < 1.0e-4, "silent before any note");

    host.midi.addEvent (juce::MidiMessage::noteOn (1, loudNote, 1.0f), 0);
    const auto loud = rms (40);   // about 0.2 s of the slice
    host.midi.addEvent (juce::MidiMessage::noteOff (1, loudNote), 0);
    rms (20);                     // past the 20 ms release
    require (rms (8) < 1.0e-4, "silent after note-off");

    host.midi.addEvent (juce::MidiMessage::noteOn (1, quietNote, 1.0f), 0);
    const auto quiet = rms (40);

    require (loud > 0.1, "the first note plays (rms " + std::to_string (loud) + ")");
    require (quiet > 0.01 && quiet < loud * 0.5,
             "the second note plays the quieter slice (rms " + std::to_string (quiet) + " vs " + std::to_string (loud) + ")");
}

// Adding, removing, undoing and redoing samples while voices play. RealtimeSanitizer builds
// (-DINTERSECT_SANITIZE=realtime) also check that none of it allocates, frees or locks in processBlock.
void kitEditsWhilePlaying()
{
    TempDir temp;
    const auto first = writeSine (temp.dir.getChildFile ("first.wav"), (int) kRate);
    const auto second = writeSine (temp.dir.getChildFile ("second.wav"), (int) kRate / 2);

    Host host;
    host.loadKit (first, 2);
    const auto playNotes = [&host]
    {
        for (int note = 36; note < 38; ++note)
            host.midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
    };

    playNotes();
    host.processor.addFilesToKit ({ second });
    host.runUntil ([&] { return host.ui().numSessionSamples == 2; }, "the second sample to be added");
    const int secondId = host.ui().sessionSamples[1].sampleId;
    require (host.ui().sessionSamples[1].fileName.toString() == "second.wav", "added at the end");

    playNotes();
    host.processor.deleteSessionSampleAsync (secondId);
    host.runUntil ([&] { return host.ui().numSessionSamples == 1; }, "the second sample to be removed");
    require (host.ui().numSlices == 2, "the first sample keeps its slices");
    require (host.ui().sessionSamples[0].fileName.toString() == "first.wav", "the first sample stays");

    playNotes();
    host.push (IntersectProcessor::CmdUndo);
    host.runUntil ([&] { return host.ui().numSessionSamples == 2; }, "undo to bring the sample back");
    require (host.ui().sessionSamples[1].sampleId == secondId, "undo brings back the same sample");

    playNotes();
    host.push (IntersectProcessor::CmdRedo);
    host.runUntil ([&] { return host.ui().numSessionSamples == 1; }, "redo to remove it again");
    require (host.ui().numSlices == 2, "redo keeps the first sample's slices");
}

// A host restore or preset load while audio plays must not race the audio thread. The audio
// thread does what JUCE's plug-in wrappers do around each block: take the callback lock and
// skip processBlock while suspended. Run under TSan to see races; plain builds only catch crashes.
void restoreWhilePlaying()
{
    TempDir temp;
    const auto wav = writeSine (temp.dir.getChildFile ("loop.wav"), (int) kRate);

    Host host;
    host.loadKit (wav, 4);
    const auto saved = host.save();

    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (host.processor.getTotalNumOutputChannels(), kBlock);
        juce::MidiBuffer midi;
        for (int n = 0; ! stop.load(); ++n)
        {
            midi.clear();
            if (n % 8 == 0)
                for (int note = 36; note < 40; ++note)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);

            {
                const juce::ScopedLock lock (host.processor.getCallbackLock());
                if (host.processor.isSuspended())
                    buffer.clear();
                else
                    host.processor.processBlock (buffer, midi);
            }
            blocks.fetch_add (1);
        }
    });

    for (int i = 0; i < 100; ++i)
    {
        host.restore (saved);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
    }

    stop = true;
    audio.join();
    require (blocks.load() > 0, "the audio thread rendered");
}
}

// A save (or a DAW autosave) while edits are being applied on the audio thread must see whole
// edits. The audio thread renders and applies a stream of slice edits while this thread saves
// repeatedly; every save must read back. Run under TSan to see whether the save reads engine
// state the audio thread is writing.
void saveWhileEditing()
{
    TempDir temp;
    const auto wav = writeSine (temp.dir.getChildFile ("edits.wav"), (int) kRate);

    Host host;
    host.loadKit (wav, 8);

    std::atomic<bool> stop { false };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (host.processor.getTotalNumOutputChannels(), kBlock);
        juce::MidiBuffer midi;
        while (! stop.load())
        {
            const juce::ScopedLock lock (host.processor.getCallbackLock());
            if (host.processor.isSuspended())
                buffer.clear();
            else
                host.processor.processBlock (buffer, midi);
        }
    });

    for (int i = 0; i < 150; ++i)
    {
        host.setSliceParam (i % 8, IntersectProcessor::FieldPitch, (float) (i % 25) - 12.0f);
        host.setSliceParam (i % 8, IntersectProcessor::FieldVolume, -(float) (i % 13));
        const auto saved = host.save();
        const auto read = StateFormat::read (saved.getData(), saved.getSize());
        require (read.state.has_value() && read.state->slices.size() == 8, "save " + std::to_string (i) + " reads back with every slice");
        juce::Thread::sleep (1);
    }

    stop = true;
    audio.join();
}

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String group = argc > 1 ? argv[1] : "state";

    int failed = 0;
    int passed = 0;
    const auto run = [&] (const char* name, auto test)
    {
        try
        {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    if (group == "state")
    {
        run ("state round trip", stateRoundTrips);
        run ("saved-state fixtures load", fixturesLoad);
        run ("a rich kit saves the expected bytes", richKitSavesExpectedBytes);
        run ("slice field edits save the expected bytes", sliceFieldEditsSaveExpectedBytes);
        run ("slice edits on their own", sliceEditsOnTheirOwn);
        run ("slice commands save the expected bytes", sliceCommandsSaveExpectedBytes);
        run ("a damaged state changes nothing", damagedStateChangesNothing);
        run ("a missing sample keeps its slices' loops", missingSampleKeepsLoopPoints);
        run ("notes play their slices, note-off ends them", notesPlayTheirSlices);
        run ("add, remove, undo and redo samples while playing", kitEditsWhilePlaying);
    }
    else if (group == "stress")
    {
        run ("restore while audio renders", restoreWhilePlaying);
        run ("save while editing", saveWhileEditing);
    }
    else
    {
        std::cerr << "unknown test group: " << group << " (state, stress)\n";
        return 2;
    }

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
