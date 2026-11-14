#include "UrlImportJob.h"
#include "ToolProcess.h"

namespace UrlImport
{
namespace
{
constexpr int kTailLines = 200;

struct YtDlpRun
{
    juce::String startError;
    bool cancelled = false;
    int exitCode = -1;
    OutputLine info;
    OutputLine done;
    juce::String lastError;
    juce::StringArray tail;   // everything but progress lines, for COPY DETAILS and classification

    void keep (const juce::String& line)
    {
        tail.add (line);
        if (tail.size() > kTailLines)
            tail.remove (0);
    }
};

YtDlpRun runYtDlp (const InstalledTools& tools, const ImportRequest& request, const juce::File& workFolder,
                   const juce::File& cacheDir, const InstallContext& context)
{
    YtDlpRun run;
    const YtDlpPaths paths { tools.ffmpeg, tools.qjs, cacheDir, workFolder };

    context.progress ("Contacting the site", -1.0f);
    ToolProcess process;
    if (! process.start (tools.ytDlp, buildYtDlpArgs (paths, request.url, request.allowLong, request.format), run.startError))
        return run;

    for (;;)
    {
        if (context.cancelled())
        {
            process.terminate();
            run.cancelled = true;
            return run;
        }

        juce::String line;
        const auto read = process.readLine (line, 150);
        if (read == ToolProcess::ReadResult::timeout)
            continue;
        if (read == ToolProcess::ReadResult::finished)
            break;

        const auto parsed = parseOutputLine (line);
        switch (parsed.kind)
        {
            case OutputLine::Kind::download:
                context.progress ("Downloading audio", downloadFraction (parsed));
                break;
            case OutputLine::Kind::postprocess:
                if (parsed.status == "started" && parsed.postprocessor == "ExtractAudio")
                    context.progress ("Converting to " + codecName (request.format), -1.0f);
                run.keep (line);
                break;
            case OutputLine::Kind::info:
                run.info = parsed;
                run.keep (line);
                break;
            case OutputLine::Kind::done:
                run.done = parsed;
                run.keep (line);
                break;
            case OutputLine::Kind::error:
                run.lastError = parsed.text;
                run.keep (line);
                break;
            case OutputLine::Kind::warning:
            case OutputLine::Kind::other:
                run.keep (line);
                break;
        }
    }

    auto code = process.waitForExit (10000);
    if (! code.has_value())
    {
        process.terminate();
        code = process.waitForExit (2000);
    }
    run.exitCode = code.value_or (-1);
    return run;
}

juce::String formatDuration (double seconds)
{
    const auto minutes = juce::roundToInt (seconds / 60.0);
    return minutes >= 60 ? juce::String (minutes / 60) + " h " + juce::String (minutes % 60) + " min"
                         : juce::String (minutes) + " min";
}

/** Moves a file, copying across drives when a rename can't. */
bool moveFile (const juce::File& source, const juce::File& destination)
{
    if (source.moveFileTo (destination))
        return true;
    return source.copyFileTo (destination) && source.deleteFile();
}

juce::File placeInFolder (const juce::File& source, const juce::File& folder, const juce::String& title,
                          const juce::String& id, juce::String& error)
{
    if (const auto created = folder.createDirectory(); created.failed())
    {
        error = "couldn't create " + folder.getFullPathName() + " (" + created.getErrorMessage() + ")";
        return {};
    }

    // Named after the file ffmpeg wrote, so the extension always matches what's inside.
    const auto destination = uniqueFileIn (folder, makeOutputFileName (title, id, source.getFileExtension()));
    if (! moveFile (source, destination))
    {
        error = "couldn't save " + destination.getFullPathName();
        return {};
    }
    return destination;
}

juce::String sandboxProblem()
{
   #if JUCE_MAC
    if (juce::SystemStats::getOperatingSystemType() < juce::SystemStats::MacOSX_10_15)
        return "URL import needs macOS 10.15 or later (yt-dlp's minimum).";
    if (juce::SystemStats::getEnvironmentVariable ("APP_SANDBOX_CONTAINER_ID", {}).isNotEmpty())
        return "This host runs plugins in a sandbox that can't start helper programs. Use the INTERSECT standalone app to import from a URL.";
   #endif
    return {};
}
} // namespace

//==============================================================================

UrlImportJob::UrlImportJob (juce::File toolsRoot) : juce::Thread ("UrlImport"), install (std::move (toolsRoot)) {}

UrlImportJob::~UrlImportJob()
{
    shouldCancel.store (true, std::memory_order_release);
    signalThreadShouldExit();
    stopThread (15000);   // a running yt-dlp gets SIGTERM, then SIGKILL after 2 s
}

bool UrlImportJob::begin (JobResult::Mode newMode)
{
    if (isBusy())
        return false;
    if (isThreadRunning() && ! waitForThreadToExit (1000))   // the previous task's thread is still winding down
        return false;

    {
        const juce::ScopedLock sl (lock);
        mode = newMode;
        stage = {};
        result = {};
    }
    progress.store (-1.0f, std::memory_order_release);
    shouldCancel.store (false, std::memory_order_release);
    phase.store (Phase::running, std::memory_order_release);
    startThread (juce::Thread::Priority::background);
    return true;
}

bool UrlImportJob::startImport (ImportRequest newRequest)
{
    if (isBusy())
        return false;
    {
        const juce::ScopedLock sl (lock);
        request = std::move (newRequest);
    }
    return begin (JobResult::Mode::import);
}

bool UrlImportJob::startPlace (juce::File source, juce::File destination, juce::File workFolder, juce::String title)
{
    if (isBusy())
        return false;
    {
        const juce::ScopedLock sl (lock);
        placeSource = std::move (source);
        placeDestination = std::move (destination);
        placeWorkFolder = std::move (workFolder);
        placeTitle = std::move (title);
    }
    return begin (JobResult::Mode::place);
}

bool UrlImportJob::startUpdate (Channel channel)
{
    if (isBusy())
        return false;
    {
        const juce::ScopedLock sl (lock);
        updateChannel = channel;
    }
    return begin (JobResult::Mode::update);
}

bool UrlImportJob::startRemove()
{
    return begin (JobResult::Mode::remove);
}

void UrlImportJob::cancel()
{
    shouldCancel.store (true, std::memory_order_release);
    signalThreadShouldExit();
}

JobResult::Mode UrlImportJob::getMode() const
{
    const juce::ScopedLock sl (lock);
    return mode;
}

juce::String UrlImportJob::getUrl() const
{
    const juce::ScopedLock sl (lock);
    return request.url;
}

juce::String UrlImportJob::getStage() const
{
    const juce::ScopedLock sl (lock);
    return stage;
}

std::optional<JobResult> UrlImportJob::consumeResult()
{
    if (phase.load (std::memory_order_acquire) != Phase::finished)
        return std::nullopt;

    JobResult taken;
    {
        const juce::ScopedLock sl (lock);
        taken = std::move (result);
        result = {};
        stage = {};
    }
    progress.store (-1.0f, std::memory_order_release);
    phase.store (Phase::idle, std::memory_order_release);
    return taken;
}

void UrlImportJob::run()
{
    // Fires after the result is stored and the phase says finished, however run() exits.
    struct TerminalNotifier
    {
        std::function<void()>& callback;
        ~TerminalNotifier() { if (callback) callback(); }
    } notify { onTerminalState };

    const InstallContext context { *this, shouldCancel, [this] (const juce::String& newStage, float fraction)
    {
        {
            const juce::ScopedLock sl (lock);
            stage = newStage;
        }
        progress.store (fraction, std::memory_order_release);
    } };

    JobResult::Mode currentMode;
    ImportRequest currentRequest;
    Channel channel;
    {
        const juce::ScopedLock sl (lock);
        currentMode = mode;
        currentRequest = request;
        channel = updateChannel;
    }

    JobResult outcome;
    switch (currentMode)
    {
        case JobResult::Mode::import: outcome = runImport (context, currentRequest); break;
        case JobResult::Mode::place:  outcome = runPlace (context); break;
        case JobResult::Mode::update: outcome = runUpdate (context, channel); break;
        case JobResult::Mode::remove: outcome = runRemove (context); break;
    }
    outcome.mode = currentMode;

    {
        const juce::ScopedLock sl (lock);
        result = std::move (outcome);
    }
    phase.store (Phase::finished, std::memory_order_release);
}

JobResult UrlImportJob::runImport (const InstallContext& context, const ImportRequest& importRequest)
{
    JobResult out;
    out.request = importRequest;

    auto cancelled = [&out]
    {
        out.status = JobResult::Status::cancelled;
        out.message = "URL import cancelled";
        return out;
    };
    auto failed = [&out] (const juce::String& message)
    {
        out.status = JobResult::Status::failed;
        out.message = message;
        return out;
    };

    if (const auto problem = sandboxProblem(); problem.isNotEmpty())
        return failed (problem);
    if (! isWebUrl (importRequest.url))
        return failed ("That doesn't look like a web link (it should start with https://).");

    auto tools = install.ensure (context, importRequest.channel, false);
    if (tools.cancelled)
        return cancelled();
    if (tools.error.isNotEmpty())
        return failed ("Couldn't set up URL import: " + tools.error);
    out.warning = tools.warning;

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const auto workFolder = install.getStagingDir().getChildFile ("import-" + juce::Uuid().toString());
        workFolder.createDirectory();

        const auto run = runYtDlp (tools.tools, importRequest, workFolder, install.getCacheDir(), context);
        out.details = run.tail.joinIntoString ("\n");
        if (run.cancelled)
        {
            workFolder.deleteRecursively();
            return cancelled();
        }
        if (run.startError.isNotEmpty())
        {
            workFolder.deleteRecursively();
            return failed ("yt-dlp couldn't start (" + run.startError + "). Try UPDATE NOW in Settings > URL Import.");
        }

        // Success: the after_move line names the converted file.
        auto file = run.done.kind == OutputLine::Kind::done ? juce::File::createFileWithoutCheckingPath (run.done.filepath)
                                                            : juce::File();
        if (! file.existsAsFile() && run.done.kind == OutputLine::Kind::done)
            file = workFolder.getChildFile ("dl." + fileExtension (importRequest.format));
        if (file.existsAsFile())
        {
            out.title = run.done.title.isNotEmpty() ? run.done.title : run.info.title;
            out.duration = run.done.duration > 0.0 ? run.done.duration : run.info.duration;
            if (importRequest.destinationFolder != juce::File())
            {
                context.progress ("Saving", -1.0f);
                juce::String error;
                out.file = placeInFolder (file, importRequest.destinationFolder, out.title, run.done.id, error);
                workFolder.deleteRecursively();
                if (out.file == juce::File())
                    return failed ("The import worked but " + error + ".");
            }
            else
            {
                out.file = file;
                out.workFolder = workFolder;   // the save dialog decides where it goes
            }
            out.status = JobResult::Status::completed;
            out.message = "Imported \"" + out.title + "\"";
            return out;
        }

        // Exited cleanly without a file: the match filter turned it down. [ix-info] says why.
        const bool exitedCleanly = run.exitCode == 0 || run.exitCode == -1;
        if (exitedCleanly && run.lastError.isEmpty() && run.info.kind == OutputLine::Kind::info)
        {
            workFolder.deleteRecursively();
            out.title = run.info.title;
            out.duration = run.info.duration;
            if (run.info.isLive)
            {
                out.status = JobResult::Status::liveRefused;
                out.message = "That's a live stream. INTERSECT can only import videos and tracks that have finished.";
                return out;
            }
            if (run.info.duration > kLongImportSeconds && ! importRequest.allowLong)
            {
                const auto gigabytes = run.info.duration * 48000.0 * 2.0 * 4.0 / 1.0e9;   // stereo float at 48 kHz
                out.status = JobResult::Status::needsLongConfirm;
                out.message = "\"" + run.info.title + "\" is " + formatDuration (run.info.duration)
                            + " long. It will use about " + juce::String (gigabytes, 1)
                            + " GB of memory and disk. Import anyway?";
                return out;
            }
        }

        workFolder.deleteRecursively();
        const auto kind = classifyFailure (run.tail);

        // Sites change; a newer yt-dlp usually copes. An ffmpeg without the chosen encoder needs newer
        // audio tools. Check now (not in 6 h) and retry once if either arrived.
        if (attempt == 0 && failureSuggestsUpdate (kind))
        {
            auto retry = install.ensure (context, importRequest.channel, true);
            if (retry.cancelled)
                return cancelled();
            if (retry.error.isEmpty() && (retry.ytDlpChanged || retry.mediaChanged))
            {
                tools = std::move (retry);
                continue;
            }
        }

        if (kind == FailureKind::other && run.lastError.isEmpty() && run.exitCode > 0)
            return failed ("yt-dlp stopped with exit code " + juce::String (run.exitCode) + ". COPY DETAILS has its output.");
        return failed (describeFailure (kind, run.lastError));
    }

    return failed ("The import failed.");
}

JobResult UrlImportJob::runPlace (const InstallContext& context)
{
    JobResult out;
    juce::File source, destination, workFolder;
    {
        const juce::ScopedLock sl (lock);
        source = placeSource;
        destination = placeDestination;
        workFolder = placeWorkFolder;
        out.title = placeTitle;
    }

    context.progress ("Saving", -1.0f);
    destination.getParentDirectory().createDirectory();
    if (destination != source)
        destination.deleteFile();   // the save dialog already asked about replacing it
    const bool moved = destination == source || moveFile (source, destination);
    if (workFolder != juce::File())
        workFolder.deleteRecursively();

    if (! moved)
    {
        out.status = JobResult::Status::failed;
        out.message = "Couldn't save " + destination.getFullPathName();
        return out;
    }

    out.status = JobResult::Status::completed;
    out.file = destination;
    out.message = "Imported \"" + out.title + "\"";
    return out;
}

JobResult UrlImportJob::runUpdate (const InstallContext& context, Channel channel)
{
    JobResult out;
    const auto before = install.readInstalled();
    const auto outcome = install.ensure (context, channel, true);
    if (outcome.cancelled)
    {
        out.status = JobResult::Status::cancelled;
        out.message = "Update cancelled";
        return out;
    }
    if (outcome.error.isNotEmpty())
    {
        out.status = JobResult::Status::failed;
        out.message = "Couldn't update the URL import tools: " + outcome.error;
        return out;
    }

    out.status = JobResult::Status::completed;
    out.warning = outcome.warning;
    const auto& tools = outcome.tools;
    const bool changed = tools.ytDlpVersion != before.ytDlpVersion || tools.mediaRelease != before.mediaRelease
                      || before.ytDlpChannel != tools.ytDlpChannel || ! before.isComplete();
    out.message = (changed ? "URL import tools updated: yt-dlp " : "URL import tools are up to date: yt-dlp ")
                + tools.ytDlpVersion + ", ffmpeg " + tools.ffmpegVersion + ", QuickJS " + tools.qjsVersion;
    if (outcome.warning.isNotEmpty())
        out.message = outcome.warning;
    return out;
}

JobResult UrlImportJob::runRemove (const InstallContext& context)
{
    JobResult out;
    juce::String error;
    if (install.removeAll (context, error))
    {
        out.status = JobResult::Status::completed;
        out.message = "Removed the URL import tools. They download again on the next import.";
    }
    else
    {
        out.status = error == "cancelled" ? JobResult::Status::cancelled : JobResult::Status::failed;
        out.message = "Couldn't remove the URL import tools: " + error;
    }
    return out;
}
} // namespace UrlImport
