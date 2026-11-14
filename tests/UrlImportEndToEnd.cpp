// End-to-end URL import outside the plugin: installs the tools into a fresh folder, imports one
// link with the same job the plugin runs, and checks the file. Needs the network.
//
//   INTERSECT_MEDIA_TOOLS_MANIFEST=file:///.../intersect_media_tools_manifest.json \
//     IntersectUrlImportE2E <url> <tools folder> <output folder> [nightly] [format=flac-24] [rate=16000]
//
// format is a settings value (wav-f32, wav-24, wav-16, flac-24, flac-16); rate is in Hz.
//
// Only use links to material you own or that is freely licensed (the project's own videos).
#include "urlimport/UrlImportJob.h"
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cerr << "usage: IntersectUrlImportE2E <url> <tools folder> <output folder> [nightly] [format=...] [rate=...]\n";
        return 2;
    }

    const juce::String url = juce::String::fromUTF8 (argv[1]);
    const juce::File toolsRoot = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2]));
    const juce::File outputFolder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[3]));
    bool nightly = false;
    UrlImport::OutputFormat format;
    int rate = 0;
    for (int i = 4; i < argc; ++i)
    {
        const juce::String option (argv[i]);
        if (option == "nightly")
            nightly = true;
        else if (option.startsWith ("format="))
            format = UrlImport::formatFromString (option.fromFirstOccurrenceOf ("=", false, false));
        else if (option.startsWith ("rate="))
            rate = option.fromFirstOccurrenceOf ("=", false, false).getIntValue();
    }
    format.sampleRate = UrlImport::isValidSampleRate (rate) ? rate : 0;
    std::cout << "format: " << UrlImport::formatToString (format) << (format.sampleRate > 0 ? " @ " + juce::String (format.sampleRate) + " Hz" : juce::String (" @ source rate")) << '\n';

    UrlImport::UrlImportJob job (toolsRoot);
    UrlImport::ImportRequest request;
    request.url = url;
    request.channel = nightly ? UrlImport::Channel::nightly : UrlImport::Channel::stable;
    request.destinationFolder = outputFolder;
    request.format = format;

    const auto started = juce::Time::getMillisecondCounter();
    if (! job.startImport (request))
    {
        std::cerr << "couldn't start\n";
        return 1;
    }

    juce::String lastLine;
    while (job.isRunning())
    {
        const auto progress = job.getProgress();
        juce::String line = job.getStage();
        if (progress >= 0.0f)
            line << " " << juce::roundToInt (progress * 10.0f) * 10 << "%";   // 10% steps keep the log short
        if (line != lastLine)
        {
            std::cout << "  [" << (juce::Time::getMillisecondCounter() - started) / 100 / 10.0 << "s] " << line << '\n';
            lastLine = line;
        }
        juce::Thread::sleep (100);
    }

    const auto result = job.consumeResult();
    if (! result.has_value())
    {
        std::cerr << "no result\n";
        return 1;
    }

    const auto tools = job.readInstalled();
    std::cout << "tools: yt-dlp " << tools.ytDlpVersion << ", ffmpeg " << tools.ffmpegVersion
              << ", QuickJS " << tools.qjsVersion << " (" << tools.mediaRelease << ")\n";
    std::cout << "status " << (int) result->status << ": " << result->message << '\n';
    if (result->warning.isNotEmpty())
        std::cout << "warning: " << result->warning << '\n';
    if (result->status != UrlImport::JobResult::Status::completed)
    {
        std::cout << result->details << '\n';
        return 1;
    }

    std::cout << "file: " << result->file.getFullPathName() << " (" << result->file.getSize() << " bytes) in "
              << (juce::Time::getMillisecondCounter() - started) / 1000.0 << " s\n";
    return result->file.existsAsFile() && result->file.getSize() > 1000 ? 0 : 1;
}
