#include "audio/SampleData.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Missing-sample placeholders: a kit with absent or unreadable files still decodes, keeping
// every sample's id, order and path so it can be relinked later.
namespace
{
constexpr double kRate = 44100.0;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

juce::File writeSine (const juce::File& file, int numFrames)
{
    juce::AudioBuffer<float> buffer (1, numFrames);
    for (int i = 0; i < numFrames; ++i)
        buffer.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * i / 100.0));

    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (new juce::FileOutputStream (file), kRate, 1, 16, {}, 0));
    require (writer != nullptr, "could not write test wav");
    writer->writeFromAudioSampleBuffer (buffer, 0, numFrames);
    return file;
}

struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("intersect-sampledata-test", {});
    TempDir()  { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

void keepsMissingSamplesAsPlaceholdersInOrder()
{
    TempDir temp;
    const auto first = writeSine (temp.dir.getChildFile ("a.wav"), 44100);
    const auto missing = temp.dir.getChildFile ("gone.wav");
    const auto third = writeSine (temp.dir.getChildFile ("c.wav"), 3 * 44100);
    const auto unreadable = temp.dir.getChildFile ("junk.wav");
    unreadable.replaceWithText ("not audio");

    const std::vector<int> ids { 11, 22, 33, 44 };
    auto decoded = SampleData::decodeFromFiles ({ first, missing, third, unreadable }, kRate, &ids);
    require (decoded != nullptr, "a kit with missing files must still decode");

    const auto& samples = decoded->sessionSamples;
    require (samples.size() == 4, "every file keeps a session sample");
    for (size_t i = 0; i < samples.size(); ++i)
        require (samples[i].sampleId == ids[i], "ids stay in their original order");

    using Availability = SampleData::SessionSample::Availability;
    require (samples[0].availability == Availability::loaded, "existing file loads");
    require (samples[1].availability == Availability::missing, "absent file is a missing placeholder");
    require (samples[2].availability == Availability::loaded, "files after a missing one still load");
    require (samples[3].availability == Availability::unreadable, "undecodable file is an unreadable placeholder");
    require (samples[1].filePath == missing.getFullPathName(), "placeholder keeps the saved path");
    require (decoded->hasPlaceholders(), "snapshot reports placeholders");

    // Placeholders share one length: the average loaded sample (2 s here), within 1-10 s.
    require (samples[1].numFrames == samples[3].numFrames, "placeholders are the same size");
    require (samples[1].numFrames == 2 * 44100, "placeholder is an average loaded sample long");

    int expectedStart = 0;
    for (const auto& sample : samples)
    {
        require (sample.startFrame == expectedStart, "session samples are contiguous");
        expectedStart += sample.numFrames;
    }
    require (decoded->buffer.getNumSamples() == expectedStart, "buffer covers every slot");

    const auto& placeholder = samples[1];
    require (decoded->buffer.getMagnitude (placeholder.startFrame, placeholder.numFrames) == 0.0f,
             "placeholder slot is silent");
    require (decoded->buffer.getMagnitude (samples[2].startFrame, samples[2].numFrames) > 0.1f,
             "real audio lands after the placeholder");
}

void allMissingSamplesShareTheViewEvenly()
{
    TempDir temp;
    const std::vector<int> ids { 1, 2, 3 };
    auto decoded = SampleData::decodeFromFiles ({ temp.dir.getChildFile ("x.wav"),
                                                  temp.dir.getChildFile ("y.wav"),
                                                  temp.dir.getChildFile ("z.wav") },
                                                kRate, &ids);
    require (decoded != nullptr, "an all-missing kit still decodes");
    require (decoded->sessionSamples.size() == 3, "every missing file keeps a slot");
    for (const auto& sample : decoded->sessionSamples)
    {
        require (sample.isPlaceholder(), "all slots are placeholders");
        require (sample.numFrames == 2 * 44100, "each slot is 2 s");
    }
    require (decoded->decodedSampleRate == kRate, "placeholders use the project rate");
}
} // namespace

int runSampleDataTests (int& passed, int& failed)
{
    const auto run = [&] (const char* name, void (*test)())
    {
        try
        {
            test();
            ++passed;
            std::cout << "PASS SampleData: " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL SampleData: " << name << ": " << error.what() << '\n';
        }
    };
    run ("missing samples become placeholders in order", keepsMissingSamplesAsPlaceholdersInOrder);
    run ("all-missing kit shares the view evenly", allMissingSamplesShareTheViewEvenly);
    return failed;
}
