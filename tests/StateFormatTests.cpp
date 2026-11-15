// The saved-state format on its own (state/SessionState), without a processor: every fixture
// reads, the expected files read and write back byte for byte, unsupported versions and damaged
// data are rejected, and no truncation of a real file makes the reader misbehave.

#include "state/SessionState.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

const juce::File fixturesDir { INTERSECT_TEST_FIXTURES_DIR };

// The base version is the first int; juce streams write ints little-endian.
void setVersion (juce::MemoryBlock& block, int version)
{
    const auto bytes = juce::ByteOrder::swapIfBigEndian ((juce::uint32) version);
    std::memcpy (block.getData(), &bytes, sizeof (bytes));
}

juce::MemoryBlock load (const juce::File& file)
{
    juce::MemoryBlock block;
    require (file.loadFileAsData (block), "could not read " + file.getFullPathName().toStdString());
    return block;
}

juce::MemoryBlock write (const SessionState& state)
{
    juce::MemoryBlock block;
    juce::MemoryOutputStream out (block, false);
    StateFormat::write (state, out);
    out.flush();
    return block;
}

void everyFixtureReads()
{
    auto files = fixturesDir.findChildFiles (juce::File::findFiles, false, "*.bin");
    require (! files.isEmpty(), "no fixtures");
    for (const auto& file : files)
    {
        const auto name = file.getFileName().toStdString();
        const auto info = juce::JSON::parse (file.withFileExtension ("json"));
        const auto blob = load (file);
        const auto read = StateFormat::read (blob.getData(), blob.getSize());
        require (read.state.has_value(), name + " reads (" + read.error.toStdString() + ")");
        require (read.state->version == (int) info["stateVersion"], name + ": version");
        require ((int) read.state->slices.size() == juce::jmin ((int) info["numSlices"], 128), name + ": slices");
        // Session-sample lists start with extension 4; older states name their one file in filePath.
        const int extension = info["extensionVersion"];
        require ((int) read.state->sessionSamples.size() == (extension >= 4 ? (int) info["numSamples"] : 0), name + ": samples");
        require (extension >= 4 || read.state->filePath.isNotEmpty(), name + ": the legacy file path");
        require (read.state->sampleParams.has_value() == ((int) info["extensionVersion"] >= 7), name + ": per-sample params from extension 7 on");
    }
}

// Reading sanitises (e.g. a parked slice's 1-sample stub becomes the 64-sample minimum), so the
// format must be stable after one read: reading and writing again changes nothing. A regular kit
// (rich-kit.bin) needs no sanitising and writes back exactly.
void writingIsStableAndExactForARegularKit()
{
    auto files = fixturesDir.getChildFile ("expected").findChildFiles (juce::File::findFiles, false, "*.bin");
    require (! files.isEmpty(), "no expected files");
    for (const auto& file : files)
    {
        const auto name = file.getFileName().toStdString();
        const auto blob = load (file);
        const auto first = StateFormat::read (blob.getData(), blob.getSize());
        require (first.state.has_value(), name + " reads");
        const auto once = write (*first.state);
        const auto second = StateFormat::read (once.getData(), once.getSize());
        require (second.state.has_value() && write (*second.state) == once, name + ": a second read and write changes nothing");
        if (file.getFileName() == "rich-kit.bin")
            require (once == blob, name + ": read then written back gives the same bytes");
    }
}

void unsupportedVersionsAreRejected()
{
    const auto blob = load (fixturesDir.getChildFile ("expected").getChildFile ("rich-kit.bin"));
    for (const int version : { 0, 18, 25, -1 })
    {
        juce::MemoryBlock copy (blob);
        setVersion (copy, version);
        const auto read = StateFormat::read (copy.getData(), copy.getSize());
        require (! read.state.has_value() && read.error.contains ("Unsupported"),
                 "version " + std::to_string (version) + " is rejected");
    }
}

void truncatedOrGarbageDataIsHandled()
{
    // Every truncation of every expected file: the reader must stay in bounds (ASan in CI) and
    // reject anything cut before the slice data ends.
    for (const auto& file : fixturesDir.getChildFile ("expected").findChildFiles (juce::File::findFiles, false, "*.bin"))
    {
        const auto blob = load (file);
        for (size_t size = 0; size < blob.getSize(); ++size)
        {
            const auto read = StateFormat::read (blob.getData(), size);
            if (size < blob.getSize() / 2)
                require (! read.state.has_value(), file.getFileName().toStdString() + " cut to "
                                                   + std::to_string (size) + " bytes is rejected");
        }
    }

    juce::Random random (42);
    juce::MemoryBlock garbage (4096);
    for (int round = 0; round < 200; ++round)
    {
        random.fillBitsRandomly (garbage.getData(), garbage.getSize());
        setVersion (garbage, 24);   // a plausible version, garbage after it
        (void) StateFormat::read (garbage.getData(), garbage.getSize());
    }
    const auto empty = StateFormat::read (nullptr, 0);
    require (! empty.state.has_value(), "empty data is rejected");
}

// A kit with two groups, edited levels, outputs, mutes and slice member masks, written and read back.
SessionState groupedState()
{
    SessionState state;
    state.version = StateFormat::kCurrentStateVersion;
    for (int id = 1; id <= 4; ++id)
    {
        SampleData::SessionSample sample;
        sample.sampleId = id;
        sample.filePath = "/kit/sample" + juce::String (id) + ".wav";
        sample.fileName = "sample" + juce::String (id) + ".wav";
        state.sessionSamples.push_back (sample);
    }
    state.sampleParams = SampleParamTable {};

    for (int i = 0; i < 3; ++i)
    {
        Slice slice;
        slice.active = true;
        slice.sampleId = 1;
        slice.startInSample = slice.startSample = i * 1000;
        slice.endInSample = slice.endSample = (i + 1) * 1000;
        slice.midiNote = slice.highNote = slice.sliceRootNote = 36 + i;
        state.slices.push_back (slice);
    }
    state.slices[1].lockMask |= kLockMembers;
    state.slices[1].memberMask = 0b10;

    auto& group = state.sampleGroups.groups[(size_t) state.sampleGroups.addGroup (1)];
    group.addMember (2);
    group.addMember (3);
    group.gainDb[1] = -6.5f;
    group.outputBus[2] = 3;
    group.mutedMask = 0b100;
    return state;
}

void groupsRoundTrip()
{
    const auto blob = write (groupedState());
    const auto read = StateFormat::read (blob.getData(), blob.getSize());
    require (read.state.has_value(), "a grouped state reads");
    const auto& groups = read.state->sampleGroups;
    require (groups.count == 1 && groups.groups[0].numMembers == 3, "the group and its members come back");
    const auto& group = groups.groups[0];
    require (group.memberIds[0] == 1 && group.memberIds[1] == 2 && group.memberIds[2] == 3, "member order is kept");
    require (juce::exactlyEqual (group.gainDb[1], -6.5f) && group.outputBus[2] == 3 && group.outputBus[0] == -1
                 && group.mutedMask == 0b100u,
             "levels, outputs and mutes come back");
    require (read.state->slices[1].memberMask == 0b10u && (read.state->slices[1].lockMask & kLockMembers) != 0,
             "a slice's own members come back");
    require (read.state->slices[0].memberMask == 0u && (read.state->slices[0].lockMask & kLockMembers) == 0,
             "other slices follow the group mix");
    require (write (*read.state) == blob, "written back, the bytes are the same");
}

void groupsAreCheckedWhenRead()
{
    auto state = groupedState();
    auto& second = state.sampleGroups.groups[(size_t) state.sampleGroups.addGroup (4)];
    second.addMember (99);   // not in the kit: the group falls to one member and goes
    state.sampleGroups.groups[0].addMember (99);
    const auto blob = write (state);
    const auto read = StateFormat::read (blob.getData(), blob.getSize());
    require (read.state.has_value(), "the state still reads");
    require (read.state->sampleGroups.count == 1 && read.state->sampleGroups.groups[0].numMembers == 3,
             "unknown members are dropped, and a group left with one member goes");

    // Cut inside the v9 block: the kit loads, ungrouped.
    const auto clean = write (groupedState());
    const auto cut = StateFormat::read (clean.getData(), clean.getSize() - 10);
    require (cut.state.has_value() && cut.state->sampleGroups.count == 0 && cut.state->sessionSamples.size() == 4,
             "a damaged group block leaves the kit loaded but ungrouped");
}

void groupModelHelpers()
{
    require (SampleGroups::lengthsMatch (48000, 48001, 48000.0), "one frame longer matches (stems after resampling)");
    require (SampleGroups::lengthsMatch (48000, 48480, 48000.0), "10 ms longer matches");
    require (! SampleGroups::lengthsMatch (48000, 48481, 48000.0), "more than 10 ms does not");
    require (SampleGroups::maskAfterRemoval (0b1011u, 1) == 0b101u, "masks shift down past the removed member");
    require (SampleGroups::maskAfterRemoval (0b1011u, 0) == 0b101u, "removing member 1");

    SampleGroup group;
    group.addMember (10);
    group.addMember (11);
    group.addMember (12);
    group.outputBus[2] = 4;
    group.gainDb[1] = -100.0f;
    group.mutedMask = 0b010;
    require (SampleGroups::membersPlayed (group, false, 0) == 0b101u, "a slice following the group plays the unmuted");
    require (SampleGroups::membersPlayed (group, true, 0b010) == 0b010u, "its own list overrides the mutes");

    SampleGroups::VoicePlan plan;
    SampleGroups::planVoices (group, 0b111, 0, [] (int id, int& start, int& frames)
    {
        start = id * 100;
        frames = 50;
        return id != 11;   // member 2's file is missing
    }, plan);
    require (plan.numVoices == 2, "one voice per output");
    require (plan.voices[0].outputBus == 0 && plan.voices[0].numTaps == 1 && plan.voices[0].taps[0].startFrame == 1000,
             "the slice's output gets member 1");
    require (plan.voices[1].outputBus == 4 && plan.voices[1].taps[0].startFrame == 1200, "output 5 gets member 3");

    group.removeMemberAt (0);
    require (group.numMembers == 2 && group.memberIds[0] == 11 && group.mutedMask == 0b001u && group.outputBus[1] == 4,
             "removing the anchor keeps the others' order, mutes and outputs");
}
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
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

    run ("every fixture reads", everyFixtureReads);
    run ("writing is stable, and exact for a regular kit", writingIsStableAndExactForARegularKit);
    run ("unsupported versions are rejected", unsupportedVersionsAreRejected);
    run ("truncated or garbage data is handled", truncatedOrGarbageDataIsHandled);
    run ("groups round trip (extension v9)", groupsRoundTrip);
    run ("groups are checked when read", groupsAreCheckedWhenRead);
    run ("group model helpers", groupModelHelpers);

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
