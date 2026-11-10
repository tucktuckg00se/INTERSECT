// End-to-end URL import outside the plugin: installs the tools into a fresh folder, imports one
// link with the same job the plugin runs, and checks the WAV. Needs the network.
//
//   INTERSECT_MEDIA_TOOLS_MANIFEST=file:///.../intersect_media_tools_manifest.json \
//     IntersectUrlImportE2E <url> <tools folder> <output folder> [nightly]
//
// Only use links to material you own or that is freely licensed (the project's own videos).
#include "urlimport/UrlImportJob.h"
#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cerr << "usage: IntersectUrlImportE2E <url> <tools folder> <output folder> [nightly]\n";
        return 2;
    }

    const juce::String url = juce::String::fromUTF8 (argv[1]);
    const juce::File toolsRoot = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2]));
    const juce::File outputFolder = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[3]));
    const bool nightly = argc > 4 && juce::String (argv[4]) == "nightly";

    UrlImport::UrlImportJob job (toolsRoot);
    UrlImport::ImportRequest request;
    request.url = url;
    request.channel = nightly ? UrlImport::Channel::nightly : UrlImport::Channel::stable;
    request.destinationFolder = outputFolder;

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
