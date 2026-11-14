#pragma once
#include "UrlImportLogic.h"
#include <atomic>
#include <functional>

namespace UrlImport
{
/** The worker thread a tool job runs on: progress out, cancel in. */
struct InstallContext
{
    InstallContext (juce::Thread& threadIn, std::atomic<bool>& cancelIn,
                    std::function<void (const juce::String&, float)> onProgressIn = {})
        : thread (threadIn), cancel (cancelIn), onProgress (std::move (onProgressIn)) {}

    juce::Thread& thread;
    std::atomic<bool>& cancel;
    /** Stage text and 0-1 progress (-1 when unknown). Called on the worker thread. */
    std::function<void (const juce::String& stage, float progress)> onProgress;

    bool cancelled() const { return thread.threadShouldExit() || cancel.load (std::memory_order_acquire); }
    void progress (const juce::String& stage, float fraction) const { if (onProgress) onProgress (stage, fraction); }
};

struct InstalledTools
{
    juce::File ytDlp;
    juce::File ffmpeg;
    juce::File qjs;
    juce::String ytDlpVersion;
    Channel ytDlpChannel = Channel::stable;
    juce::String mediaRelease;
    juce::String ffmpegVersion;
    juce::String qjsVersion;

    bool hasYtDlp() const { return ytDlp.existsAsFile(); }
    bool hasMediaTools() const { return ffmpeg.existsAsFile() && qjs.existsAsFile(); }
    bool isComplete() const { return hasYtDlp() && hasMediaTools(); }
};

/**
    Keeps yt-dlp and the media tools (ffmpeg, qjs) installed and current under one folder:

        <root>/state.json                 what is installed, when updates were last checked
        <root>/ytdlp/<version>-<sha12>/   yt-dlp's onedir build (current + previous)
        <root>/media/<release>-<sha12>/   ffmpeg + qjs (current + previous)
        <root>/staging/                   downloads and imports in progress
        <root>/cache/                     yt-dlp's cache

    Every download is checked against a published SHA-256 before it is installed, and installs
    land in a new folder, never over one another INTERSECT may be running. One process-wide lock
    plus an inter-process lock keep instances from installing at the same time. All blocking:
    worker threads only.
*/
class ToolInstall
{
public:
    explicit ToolInstall (juce::File rootFolder = getDefaultRoot());

    /** <data dir>/url-import (see AppFiles::getDataDir). */
    static juce::File getDefaultRoot();
    /** INTERSECT_MEDIA_TOOLS_MANIFEST, when set (a test build of the tools). */
    static juce::String getTestManifestUrl();

    juce::File getRoot() const { return root; }
    juce::File getCacheDir() const { return root.getChildFile ("cache"); }
    juce::File getStagingDir() const { return root.getChildFile ("staging"); }

    /** What is installed now, from state.json. No network; cheap enough for the message thread. */
    InstalledTools readInstalled() const;

    struct Outcome
    {
        InstalledTools tools;
        juce::String error;      // set when the tools can't be used
        juce::String warning;    // the update check failed; the installed tools are used
        bool cancelled = false;
        bool ytDlpChanged = false;
        bool mediaChanged = false;   // a newer ffmpeg + qjs was installed
    };

    /** Installs whatever is missing and, when an update check is due (6 h, a channel change) or
        forced, installs newer published versions. */
    Outcome ensure (const InstallContext& context, Channel channel, bool forceCheck);

    /** Deletes every downloaded tool. */
    bool removeAll (const InstallContext& context, juce::String& error);

private:
    class Lock;
    juce::var readState() const;
    bool writeState (const juce::var& state) const;
    juce::String installYtDlp (const InstallContext&, juce::var& state, const juce::String& asset,
                               const juce::String& sha256, Channel channel);
    juce::String installMediaTools (const InstallContext&, juce::var& state, const MediaToolsEntry& entry);
    juce::String resolveMediaManifestUrl (const InstallContext&) const;
    void collectGarbage (const juce::var& state) const;

    juce::File root;
};
} // namespace UrlImport
