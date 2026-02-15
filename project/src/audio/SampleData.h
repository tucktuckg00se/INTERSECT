#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

class SampleData
{
public:
    SampleData();

    bool loadFromFile (const juce::File& file, double projectSampleRate);

    float getInterpolatedSample (double pos, int channel) const;

    int getNumFrames() const { return buffer.getNumSamples(); }
    bool isLoaded() const { return loaded; }

    const juce::AudioBuffer<float>& getBuffer() const { return buffer; }
    const juce::String& getFileName() const { return loadedFileName; }
    void setFileName (const juce::String& name) { loadedFileName = name; }

    const juce::String& getFilePath() const { return loadedFilePath; }
    void setFilePath (const juce::String& path) { loadedFilePath = path; }

    void loadFromBuffer (juce::AudioBuffer<float>&& buf)
    {
        buffer = std::move (buf);
        loaded = buffer.getNumSamples() > 0;
    }

private:
    juce::AudioBuffer<float> buffer;  // always stereo
    juce::AudioFormatManager formatManager;
    juce::String loadedFileName;
    juce::String loadedFilePath;
    bool loaded = false;
};
