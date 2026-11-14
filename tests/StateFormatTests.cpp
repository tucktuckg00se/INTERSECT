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

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
