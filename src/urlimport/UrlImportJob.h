#pragma once
#include "ToolInstall.h"
#include <optional>

namespace UrlImport
{
struct ImportRequest
{
    juce::String url;
    Channel channel = Channel::stable;
    bool allowLong = false;          // the user confirmed an import over an hour
    juce::File destinationFolder;    // empty: leave the file in staging for a save dialog
    OutputFormat format;             // what ffmpeg writes (WAV / FLAC, bit depth, rate)
};

struct JobResult
{
    enum class Mode { import, place, update, remove };
    enum class Status { completed, failed, cancelled, needsLongConfirm, liveRefused };

    Mode mode = Mode::import;
    Status status = Status::failed;
    ImportRequest request;
    juce::File file;          // completed import / place: the audio file
    juce::File workFolder;    // import without a destination: delete once the file is placed
    juce::String title;
    double duration = -1.0;
    juce::String message;     // for the user
    juce::String details;     // yt-dlp's last output lines (COPY DETAILS)
    juce::String warning;     // non-fatal: the update check failed and installed tools were used
};

/**
    Runs one URL-import task at a time on its own thread: an import (set up the tools, run yt-dlp,
    place the WAV), placing a file the user chose a location for, updating or removing the tools.
    Owned by the processor, so an import finishes even if the editor closes. Progress is polled;
    onTerminalState fires on the job thread when a task ends.
*/
class UrlImportJob : private juce::Thread
{
public:
    /** toolsRoot: where the tools live (tests use a fresh folder). */
    explicit UrlImportJob (juce::File toolsRoot = ToolInstall::getDefaultRoot());
    ~UrlImportJob() override;

    bool startImport (ImportRequest request);
    /** Moves an imported WAV to destination (overwriting it: the save dialog already asked). */
    bool startPlace (juce::File source, juce::File destination, juce::File workFolder, juce::String title);
    bool startUpdate (Channel channel);
    bool startRemove();
    void cancel();

    /** A task is running, or has ended and its result hasn't been consumed yet. */
    bool isBusy() const noexcept { return phase.load (std::memory_order_acquire) != Phase::idle; }
    bool isRunning() const noexcept { return phase.load (std::memory_order_acquire) == Phase::running; }
    JobResult::Mode getMode() const;
    juce::String getUrl() const;
    juce::String getStage() const;
    /** 0-1, or -1 when the current stage can't say. */
    float getProgress() const noexcept { return progress.load (std::memory_order_acquire); }

    /** Message thread: the ended task's result (once), returning the job to idle. */
    std::optional<JobResult> consumeResult();

    /** What is installed now (reads state.json). */
    InstalledTools readInstalled() const { return install.readInstalled(); }

    /** Called on the job thread when a task ends. Set before the first start. */
    std::function<void()> onTerminalState;

private:
    enum class Phase { idle, running, finished };

    bool begin (JobResult::Mode newMode);
    void run() override;
    JobResult runImport (const InstallContext& context, const ImportRequest& request);
    JobResult runPlace (const InstallContext& context);
    JobResult runUpdate (const InstallContext& context, Channel channel);
    JobResult runRemove (const InstallContext& context);

    ToolInstall install;
    std::atomic<Phase> phase { Phase::idle };
    std::atomic<float> progress { -1.0f };
    std::atomic<bool> shouldCancel { false };

    mutable juce::CriticalSection lock;
    JobResult::Mode mode = JobResult::Mode::import;
    juce::String stage;
    ImportRequest request;
    JobResult result;
    juce::File placeSource, placeDestination, placeWorkFolder;
    juce::String placeTitle;
    Channel updateChannel = Channel::stable;
};
} // namespace UrlImport
