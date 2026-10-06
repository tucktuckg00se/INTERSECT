#include "AuditionPlayer.h"
#include "Rex2Import.h"
#include "../AppFiles.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace
{
// Resamples `source` to `rate`, stores it as stereo in a new clip and computes the preview peaks.
std::shared_ptr<const AuditionClip> finishClip (const juce::File& file, juce::AudioBuffer<float>& source,
                                                double sourceRate, double rate, bool truncated,
                                                const std::function<bool()>& aborted)
{
    const int numChannels = source.getNumChannels();
    const int numFrames = source.getNumSamples();

    if (std::abs (sourceRate - rate) > 0.01)
    {
        const double ratio = sourceRate / rate;
        const int resampledFrames = (int) std::ceil (numFrames / ratio);
        juce::AudioBuffer<float> resampled (numChannels, resampledFrames);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (aborted())
                return nullptr;
            juce::LagrangeInterpolator interpolator;
            interpolator.process (ratio, source.getReadPointer (ch), resampled.getWritePointer (ch), resampledFrames);
        }
        source = std::move (resampled);
    }

    auto clip = std::make_shared<AuditionClip>();
    clip->file = file;
    clip->sampleRate = rate;
    clip->truncated = truncated;
    clip->previewSeconds = (double) numFrames / sourceRate;
    const int frames = source.getNumSamples();
    clip->audio.setSize (2, frames);
    clip->audio.copyFrom (0, 0, source, 0, 0, frames);
    clip->audio.copyFrom (1, 0, source, numChannels > 1 ? 1 : 0, 0, frames);

    clip->peaks.assign ((size_t) AuditionClip::kPeakBuckets, 0.0f);
    const float* l = clip->audio.getReadPointer (0);
    const float* r = clip->audio.getReadPointer (1);
    for (int b = 0; b < AuditionClip::kPeakBuckets; ++b)
    {
        const int start = (int) ((juce::int64) frames * b / AuditionClip::kPeakBuckets);
        const int end = juce::jmax (start + 1, (int) ((juce::int64) frames * (b + 1) / AuditionClip::kPeakBuckets));
        float peak = 0.0f;
        for (int i = start; i < juce::jmin (end, frames); ++i)
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        clip->peaks[(size_t) b] = juce::jmin (1.0f, peak);
    }

    return clip;
}
} // namespace

std::shared_ptr<const AuditionClip> AuditionClip::decode (const juce::File& file,
                                                          double targetSampleRate,
                                                          double maxSeconds,
                                                          const std::function<bool()>& shouldAbort)
{
    const std::function<bool()> aborted = [&shouldAbort] { return shouldAbort != nullptr && shouldAbort(); };

    // REX2 loops bypass the JUCE format readers and decode through VelociLoops
    // (at the file's native rate; finishClip resamples like any other file).
    if (AppFiles::isRex2File (file))
    {
        Rex2Import::DecodedLoop loop;
        if (! Rex2Import::decodeFile (file, 0.0, loop) || loop.stereo.getNumSamples() <= 0)
            return nullptr;

        const auto maxFrames = (juce::int64) (maxSeconds * loop.sampleRate);
        const bool truncated = loop.stereo.getNumSamples() > maxFrames;
        if (truncated)
            loop.stereo.setSize (2, (int) maxFrames, true);

        const double rate = targetSampleRate > 0.0 ? targetSampleRate : loop.sampleRate;
        return finishClip (file, loop.stereo, loop.sampleRate, rate, truncated, aborted);
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return nullptr;

    const auto maxFrames = (juce::int64) (maxSeconds * reader->sampleRate);
    const bool truncated = reader->lengthInSamples > maxFrames;
    const int numFrames = (int) juce::jmin (reader->lengthInSamples, maxFrames);
    const int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
    juce::AudioBuffer<float> source (numChannels, numFrames);

    // Read in chunks so a superseded audition can bail out early.
    constexpr int kChunk = 1 << 16;
    for (int pos = 0; pos < numFrames; pos += kChunk)
    {
        if (aborted())
            return nullptr;
        const int n = juce::jmin (kChunk, numFrames - pos);
        reader->read (&source, pos, n, pos, true, numChannels > 1);
    }

    const double rate = targetSampleRate > 0.0 ? targetSampleRate : reader->sampleRate;
    return finishClip (file, source, reader->sampleRate, rate, truncated, aborted);
}

void AuditionPlayer::play (std::shared_ptr<const AuditionClip> clip)
{
    releaseRetiredClips();

    if (current != nullptr && current != clip)
        retired.push_back (current);
    current = std::move (clip);

    // Commands sent before this play must not apply to it; the audio thread starts from here.
    commandSequenceAtPublish = commandSequence.load();
    published = current.get();
    ++publishedGeneration;
    playing = current != nullptr;   // shows as playing now; the audio thread confirms on its next block
}

void AuditionPlayer::sendCommand (Command command)
{
    releaseRetiredClips();
    latestCommand = command;
    ++commandSequence;
}

void AuditionPlayer::pause()
{
    sendCommand (commandPause);
    playing = false;
}

void AuditionPlayer::resume()
{
    if (current == nullptr)
        return;

    sendCommand (commandResume);
    playing = true;
}

void AuditionPlayer::stop()
{
    sendCommand (commandStop);
    playing = false;
}

void AuditionPlayer::setGainDb (float db) noexcept
{
    targetGain = juce::Decibels::decibelsToGain (db, -60.0f);
}

float AuditionPlayer::getPosition() const noexcept
{
    const int length = lengthFrames.load();
    return length > 0 ? juce::jlimit (0.0f, 1.0f, (float) positionFrames.load() / (float) length) : 0.0f;
}

void AuditionPlayer::releaseRetiredClips()
{
    if (retired.empty())
        return;

    // Safe when the audio thread has switched to the newest clip, or is outside renderAdd: on its
    // next call it sees the new generation and replaces its pointer before touching any audio.
    // Both loads are sequentially consistent, matching the stores in renderAdd.
    if (acknowledgedGeneration.load() == publishedGeneration.load() || ! audioThreadRendering.load())
        retired.clear();
}

bool AuditionPlayer::renderAdd (float* left, float* right, int numSamples) noexcept
{
    audioThreadRendering = true;

    const uint32_t generation = publishedGeneration.load();
    if (generation != activeGeneration)
    {
        activeGeneration = generation;
        active = published.load();
        seenCommandSequence = commandSequenceAtPublish.load();
        readPos = 0;
        fadeInRemaining = kFadeSamples;
        fadeOutRemaining = 0;
        rewindAfterFade = false;
        activePlaying = active != nullptr;
        acknowledgedGeneration = generation;
    }

    const int length = active != nullptr ? active->audio.getNumSamples() : 0;

    if (const uint32_t sequence = commandSequence.load(); sequence != seenCommandSequence)
    {
        seenCommandSequence = sequence;
        switch (latestCommand.load())
        {
            case commandPause:
            case commandStop:
                rewindAfterFade = latestCommand.load() == commandStop;
                if (activePlaying)
                {
                    if (fadeOutRemaining == 0)
                        fadeOutRemaining = kFadeSamples;
                }
                else if (rewindAfterFade)
                {
                    readPos = 0;
                    rewindAfterFade = false;
                }
                break;

            case commandResume:
                if (active != nullptr)
                {
                    if (readPos >= length)
                        readPos = 0;
                    if (! activePlaying || fadeOutRemaining > 0)
                        fadeInRemaining = kFadeSamples;
                    fadeOutRemaining = 0;
                    rewindAfterFade = false;
                    activePlaying = true;
                }
                break;

            default:
                break;
        }
    }

    if (panicRequested)
    {
        panicRequested = false;
        activePlaying = false;
        fadeOutRemaining = 0;
    }

    bool produced = false;
    if (activePlaying && active != nullptr && numSamples > 0)
    {
        const float* srcL = active->audio.getReadPointer (0);
        const float* srcR = active->audio.getReadPointer (1);
        const float target = targetGain.load();
        const int n = juce::jmin (numSamples, length - readPos);
        const float gainStep = n > 0 ? (target - currentGain) / (float) n : 0.0f;

        for (int i = 0; i < n; ++i)
        {
            currentGain += gainStep;
            float envelope = 1.0f;
            if (fadeInRemaining > 0)
                envelope = 1.0f - (float) fadeInRemaining-- / (float) kFadeSamples;
            if (fadeOutRemaining > 0)
            {
                envelope *= (float) fadeOutRemaining / (float) kFadeSamples;
                if (--fadeOutRemaining == 0)
                {
                    activePlaying = false;   // paused (or stopped) here; readPos stays on this sample
                    break;
                }
            }

            const float g = currentGain * envelope;
            if (left != nullptr)
                left[i] += srcL[readPos] * g;
            if (right != nullptr)
                right[i] += srcR[readPos] * g;
            ++readPos;
        }

        currentGain = target;
        produced = n > 0;
        if (readPos >= length)
            activePlaying = false;
    }

    if (! activePlaying && rewindAfterFade)
    {
        readPos = 0;
        rewindAfterFade = false;
    }

    positionFrames = readPos;
    lengthFrames = length;
    playing = activePlaying;
    audioThreadRendering = false;
    return produced;
}
