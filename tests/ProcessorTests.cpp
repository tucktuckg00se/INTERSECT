// Processor-level tests: the real IntersectProcessor, driven the way a host and the editor drive it.
//
//   IntersectProcessorTests state    save/restore round trip, every saved-state fixture, kit edits while playing
//   IntersectProcessorTests stress   restores (preset/project loads) while audio is rendering
//
// The main thread is the message thread. Fixtures live in tests/fixtures (see extract_state.py).

#include "PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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

    const auto setSliceParam = [&host] (int slice, int field, float value)
    {
        IntersectProcessor::Command cmd;
        cmd.type = IntersectProcessor::CmdSetSliceParam;
        cmd.sliceIdx = slice;
        cmd.intParam1 = field;
        cmd.floatParam1 = value;
        host.processor.pushCommand (cmd);
    };
    host.push (IntersectProcessor::CmdSetSampleParam, firstId, IntersectProcessor::FieldBpm, 96.0f);
    host.push (IntersectProcessor::CmdSetSampleParam, secondId, IntersectProcessor::FieldPitch, -5.0f);
    setSliceParam (0, IntersectProcessor::FieldPitch, 7.0f);
    setSliceParam (2, IntersectProcessor::FieldBpm, 140.0f);
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
        run ("add, remove, undo and redo samples while playing", kitEditsWhilePlaying);
    }
    else if (group == "stress")
    {
        run ("restore while audio renders", restoreWhilePlaying);
    }
    else
    {
        std::cerr << "unknown test group: " << group << " (state, stress)\n";
        return 2;
    }

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
