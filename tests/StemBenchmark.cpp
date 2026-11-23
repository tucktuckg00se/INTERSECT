// Times one stem separation with the plugin's own job (StemSeparationJob), so the thread priority,
// session options, execution provider settings and segmenting are exactly what STEMS uses.
// The ONNX Runtime bundle and the model are installed with the plugin's download jobs into the
// per-user INTERSECT folders (needs the network the first time). On Linux, point HOME at a scratch
// folder so the real folders are left alone; macOS always uses the account's ~/Library and Windows
// %APPDATA%, so run it there on a test machine or account.
//
//   IntersectStemBench <model id> <cpu|gpu> <input.wav | gen:SECONDS> <output folder>
//                      [bundle=<ORT bundle folder name>] [timeout=SECONDS]
//
// e.g. HOME=/tmp/bench IntersectStemBench mini-bs-roformer-v2.5 gpu gen:60 /tmp/bench/out
//
// Prints progress, the per-segment times from the stem log, and one RESULT line for scripts.
// gen:SECONDS is a fixed synthetic mix (noise, tones, clicks): speed doesn't depend on the music.
#include "audio/StemSeparationJob.h"
#include "audio/StemModelDownloadJob.h"
#include "audio/OrtBundleDownloadJob.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
double secondsSince (juce::uint32 startMs)
{
    return (double) (juce::Time::getMillisecondCounter() - startMs) / 1000.0;
}

template <typename Job>
bool waitForDownload (Job& job, const juce::String& what)
{
    while (job.getState() == StemModelDownloadState::downloading)
        juce::Thread::sleep (100);
    const auto finalState = job.getState();
    const auto message = job.consumeResultMessage();
    if (finalState != StemModelDownloadState::completed)
    {
        std::cerr << what << " failed: " << message << '\n';
        return false;
    }
    std::cout << what << ": " << message << '\n';
    return true;
}

// The bundle named on the command line, else the platform's GPU bundle for gpu and its CPU-only
// bundle for cpu (macOS has one bundle, which also runs on the CPU).
const OrtBundleCatalogEntry* pickOrtBundle (const juce::String& wanted, StemComputeDevice device)
{
    const auto bundles = getOrtBundlesForCurrentPlatform();
    if (wanted.isNotEmpty())
    {
        for (const auto& bundle : bundles)
            if (bundle.directoryName == wanted)
                return findOrtBundleCatalogEntry (bundle.id);
        return nullptr;
    }

    for (const auto& bundle : bundles)
        if ((device == StemComputeDevice::gpu) == bundle.providerName.isNotEmpty())
            return findOrtBundleCatalogEntry (bundle.id);
    return bundles.empty() ? nullptr : findOrtBundleCatalogEntry (bundles.front().id);
}

bool ensureOrtBundle (const OrtBundleCatalogEntry& bundle)
{
    const auto ortRoot = getDefaultOrtRootFolder();
    const auto installed = scanInstalledOrtBundles (ortRoot);
    if (std::find (installed.begin(), installed.end(), bundle.id) != installed.end())
        return writeActiveOrtBundleDirectoryName (ortRoot, bundle.directoryName);

    OrtBundleDownloadJob job;
    if (! job.start (ortRoot, bundle.id, INTERSECT_ORT_VERSION))
        return false;
    return waitForDownload (job, "ONNX Runtime " + bundle.directoryName);
}

// The model's current file from the manifest; downloads the manifest and the model when missing.
juce::File ensureModel (const StemModelId& modelId, StemModelCatalogEntry& entry)
{
    const auto modelFolder = getDefaultStemModelFolder();
    entry = getEffectiveStemModelCatalogEntry (modelId, modelFolder);
    if (getStemModelManifestFile (modelFolder).existsAsFile() && entry.isValid()
        && modelFolder.getChildFile (entry.fileName).existsAsFile())
        return modelFolder.getChildFile (entry.fileName);

    StemModelDownloadJob job;
    if (! job.start (modelFolder, { modelId }) || ! waitForDownload (job, "Model " + modelId))
        return {};
    entry = getEffectiveStemModelCatalogEntry (modelId, modelFolder);
    return entry.isValid() ? modelFolder.getChildFile (entry.fileName) : juce::File();
}

juce::AudioBuffer<float> makeTestMix (double seconds, double rate)
{
    const int frames = juce::jmax (1, (int) (seconds * rate));
    juce::AudioBuffer<float> audio (2, frames);
    juce::Random random (63);
    const double twoPi = juce::MathConstants<double>::twoPi;
    for (int i = 0; i < frames; ++i)
    {
        const double t = (double) i / rate;
        const double swell = 0.5 + 0.5 * std::sin (twoPi * 0.25 * t);
        const double bass = 0.25 * std::sin (twoPi * 55.0 * t);
        const double chord = 0.08 * swell * (std::sin (twoPi * 220.0 * t) + std::sin (twoPi * 277.2 * t)
                                             + std::sin (twoPi * 329.6 * t));
        const double sinceClick = std::fmod (t, 0.5);
        const double click = sinceClick < 0.02 ? 0.5 * (1.0 - sinceClick / 0.02) * (random.nextDouble() * 2.0 - 1.0) : 0.0;
        const double noise = 0.03 * (random.nextDouble() * 2.0 - 1.0);
        audio.setSample (0, i, (float) (bass + chord * 1.2 + click + noise));
        audio.setSample (1, i, (float) (bass + chord * 0.8 + click * 0.7 + noise));
    }
    return audio;
}

bool loadAudio (const juce::String& input, juce::AudioBuffer<float>& audio, double& rate)
{
    if (input.startsWith ("gen:"))
    {
        rate = 44100.0;
        audio = makeTestMix (input.fromFirstOccurrenceOf (":", false, false).getDoubleValue(), rate);
        return true;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (input)));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    audio.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    rate = reader->sampleRate;
    return reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
}

// Per-segment seconds from the stem log's last job ("segment i/n: X s" lines after the last "---- ").
std::vector<double> readSegmentTimes (juce::String& jobLine)
{
    const auto logFile = getDefaultStemModelFolder().getParentDirectory().getChildFile ("logs").getChildFile ("stem-separation.log");
    juce::StringArray lines;
    lines.addLines (logFile.loadFileAsString());

    int start = 0;
    for (int i = lines.size(); --i >= 0;)
        if (lines[i].contains ("---- "))
        {
            start = i;
            jobLine = lines[i].fromFirstOccurrenceOf ("---- ", false, false);
            break;
        }

    std::vector<double> times;
    for (int i = start; i < lines.size(); ++i)
        if (lines[i].contains (" segment ") && lines[i].trimEnd().endsWith (" s"))
            times.push_back (lines[i].fromLastOccurrenceOf (": ", false, false).getDoubleValue());
    return times;
}

double median (std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    std::sort (values.begin(), values.end());
    return values[values.size() / 2];
}
} // namespace

int main (int argc, char* argv[])
{
    if (argc < 5)
    {
        std::cerr << "usage: IntersectStemBench <model id> <cpu|gpu> <input.wav | gen:SECONDS> <output folder>"
                     " [bundle=<ORT bundle folder>] [timeout=SECONDS]\n";
        return 2;
    }

    const StemModelId modelId = juce::String::fromUTF8 (argv[1]);
    const auto device = juce::String (argv[2]) == "gpu" ? StemComputeDevice::gpu : StemComputeDevice::cpu;
    const juce::String input = juce::String::fromUTF8 (argv[3]);
    const auto outputFolder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[4]));
    juce::String bundleName;
    double timeoutSeconds = 0.0;
    for (int i = 5; i < argc; ++i)
    {
        const juce::String option (argv[i]);
        if (option.startsWith ("bundle="))
            bundleName = option.fromFirstOccurrenceOf ("=", false, false);
        else if (option.startsWith ("timeout="))
            timeoutSeconds = option.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
    }

    const auto* bundle = pickOrtBundle (bundleName, device);
    if (bundle == nullptr)
    {
        std::cerr << "no ONNX Runtime bundle " << bundleName << " for " << getCurrentPlatformTag() << '\n';
        return 2;
    }
    if (! ensureOrtBundle (*bundle))
        return 1;

    StemModelCatalogEntry entry;
    const auto modelFile = ensureModel (modelId, entry);
    if (! modelFile.existsAsFile())
    {
        std::cerr << "model " << modelId << " isn't available\n";
        return 1;
    }

    juce::AudioBuffer<float> audio;
    double rate = 44100.0;
    if (! loadAudio (input, audio, rate))
    {
        std::cerr << "couldn't read " << input << '\n';
        return 2;
    }

    // Every output for small models; the first four for big ones (the selection only changes what is
    // kept and written, not the model's work).
    StemSelection selection;
    for (int i = 0; i < juce::jmin (entry.getNumOutputs(), entry.getNumOutputs() <= 8 ? 8 : 4); ++i)
        selection.push_back (i);

    std::cout << "model " << entry.menuLabel << " (" << modelFile.getFileName() << "), "
              << (device == StemComputeDevice::gpu ? "gpu" : "cpu") << ", bundle " << bundle->directoryName
              << ", " << audio.getNumSamples() / rate << " s of audio, " << selection.size() << " outputs\n";

    outputFolder.createDirectory();
    StemSeparationJob job;
    const auto started = juce::Time::getMillisecondCounter();
    if (! job.start (audio, rate, 1, "bench", entry, selection, StemExportMode::separate, device, modelFile, outputFolder))
    {
        std::cerr << "couldn't start\n";
        return 1;
    }

    // Progress is 0.1 until the first segment finishes (session creation included), then rises per segment.
    double firstSegmentDone = -1.0;
    bool timedOut = false;
    int lastStep = -1;
    for (;;)
    {
        const auto state = job.getState();
        if (state == StemJobState::completed || state == StemJobState::failed || state == StemJobState::cancelled)
            break;

        const auto progress = job.getProgress();
        if (firstSegmentDone < 0.0 && progress > 0.1001f)
            firstSegmentDone = secondsSince (started);
        if (const int step = (int) (progress * 10.0f); step != lastStep)
        {
            std::cout << "  [" << juce::String (secondsSince (started), 1) << " s] " << step * 10 << "%" << std::endl;
            lastStep = step;
        }
        if (! timedOut && timeoutSeconds > 0.0 && secondsSince (started) > timeoutSeconds)
        {
            std::cout << "timeout after " << timeoutSeconds << " s, cancelling" << std::endl;
            timedOut = true;
            job.cancel();
        }
        // A session that is still being created can't be cancelled: report and leave without waiting.
        if (timedOut && secondsSince (started) > timeoutSeconds + 30.0)
            break;
        juce::Thread::sleep (20);
    }

    const double total = secondsSince (started);
    const auto finalState = job.getState();
    const bool stuck = timedOut && finalState != StemJobState::completed && finalState != StemJobState::failed
                       && finalState != StemJobState::cancelled;
    const auto result = stuck ? StemJobResult {} : job.consumeResult();

    juce::String jobLine;
    const auto segments = readSegmentTimes (jobLine);
    const double firstSegment = segments.empty() ? 0.0 : segments.front();
    const double sessionCreate = firstSegmentDone >= 0.0 ? juce::jmax (0.0, firstSegmentDone - firstSegment) : total;

    std::cout << "log: " << jobLine << '\n';
    for (size_t i = 0; i < segments.size(); ++i)
        std::cout << "  segment " << i + 1 << ": " << juce::String (segments[i], 2) << " s\n";
    for (const auto& file : result.stemFiles)
        std::cout << "  wrote " << file.getFullPathName() << '\n';
    if (result.errorMessage.isNotEmpty())
        std::cout << "error: " << result.errorMessage << '\n' << result.diagnostics << '\n';

    const juce::String status = timedOut ? "timeout"
                              : finalState == StemJobState::completed ? "ok"
                              : finalState == StemJobState::cancelled ? "cancelled" : "failed";
    std::cout << "RESULT model=" << modelId << " device=" << (device == StemComputeDevice::gpu ? "gpu" : "cpu")
              << " status=" << status << " audio_s=" << juce::String (audio.getNumSamples() / rate, 1)
              << " total_s=" << juce::String (total, 2) << " session_s=" << juce::String (sessionCreate, 2)
              << " segments=" << (int) segments.size() << " median_segment_s=" << juce::String (median (segments), 2)
              << " first_segment_s=" << juce::String (firstSegment, 2) << '\n';
    std::cout.flush();
    if (stuck)
        std::_Exit (1);   // the job's destructor would wait for the stuck session
    return status == "ok" ? 0 : 1;
}
