#include "OrtBundleDownloadJob.h"
#include "../Download.h"

OrtBundleDownloadJob::OrtBundleDownloadJob()
    : juce::Thread ("OrtBundleDownload")
{
}

OrtBundleDownloadJob::~OrtBundleDownloadJob()
{
    shouldCancel.store (true, std::memory_order_release);
    stopThread (10000);
}

bool OrtBundleDownloadJob::start (juce::File ortRoot, OrtBundleId bundleId, juce::String version)
{
    if (isThreadRunning())
        return false;

    rootFolder = std::move (ortRoot);
    requestedBundle = bundleId;
    ortVersion = std::move (version);
    progress.store (0.0f, std::memory_order_release);
    currentBundleId.store (bundleId, std::memory_order_release);
    shouldCancel.store (false, std::memory_order_release);

    {
        const juce::ScopedLock sl (lock);
        statusText = {};
        resultMessage = {};
    }

    state.store (StemModelDownloadState::downloading, std::memory_order_release);
    startThread (juce::Thread::Priority::background);
    return true;
}

void OrtBundleDownloadJob::cancel()
{
    shouldCancel.store (true, std::memory_order_release);
    signalThreadShouldExit();
}

juce::String OrtBundleDownloadJob::getStatusText() const
{
    const juce::ScopedLock sl (lock);
    return statusText;
}

juce::String OrtBundleDownloadJob::consumeResultMessage()
{
    const juce::ScopedLock sl (lock);
    auto message = resultMessage;
    resultMessage = {};
    statusText = {};
    progress.store (0.0f, std::memory_order_release);
    state.store (StemModelDownloadState::idle, std::memory_order_release);
    return message;
}

void OrtBundleDownloadJob::run()
{
    // Fire onTerminalState when run() returns, regardless of which terminal
    // branch wrote the state. Lets the owner (PluginProcessor) schedule its
    // message-thread drain without the audio thread having to poll.
    struct TerminalNotifier
    {
        std::function<void()>& cb;
        ~TerminalNotifier() { if (cb) cb(); }
    } notify { onTerminalState };

    if (const auto createResult = rootFolder.createDirectory(); createResult.failed())
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Failed to create ONNX Runtime folder: " + createResult.getErrorMessage();
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
    }

    // 1. Always refresh the manifest so we can resolve the latest download URL.
    {
        juce::String downloadError;
        const auto manifestUrl = juce::URL (getOrtBundleManifestDownloadUrl());
        const auto manifestFile = getOrtBundleManifestFile (rootFolder);
        (void) Download::downloadFileToTarget (manifestUrl, manifestFile, shouldCancel, *this, downloadError);
        // Manifest is best-effort; continue on failure using whatever is on disk.
    }

    if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "ONNX Runtime download cancelled";
        state.store (StemModelDownloadState::cancelled, std::memory_order_release);
        return;
    }

    const auto entry = getEffectiveOrtBundleCatalogEntry (requestedBundle, rootFolder);
    if (entry.downloadUrl.isEmpty())
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "No download URL available for " + entry.menuLabel
                      + ". Make sure you have an internet connection and try again.";
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
    }

    {
        const juce::ScopedLock sl (lock);
        statusText = "Downloading " + entry.menuLabel;
    }

    // 2. Download the archive to the bundle version folder as a temp file.
    const auto bundleFolder = getOrtBundleInstallFolder (rootFolder, entry, ortVersion);
    if (const auto createResult = bundleFolder.createDirectory(); createResult.failed())
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Failed to create bundle folder: " + createResult.getErrorMessage();
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
    }

    const auto archiveFile = bundleFolder.getChildFile (entry.archiveFileName);
    juce::String downloadError;
    // Downloading fills the bar to 95%; extracting and activating take it the rest of the way.
    const auto onBytes = [this, expectedBytes = entry.downloadBytes] (juce::int64 received, juce::int64 total)
    {
        if (expectedBytes > 0)
            total = expectedBytes;
        if (total > 0)
            progress.store (juce::jlimit (0.0f, 0.95f, (float) ((double) received / (double) total) * 0.95f),
                            std::memory_order_release);
    };
    if (! Download::downloadFileToTarget (juce::URL (entry.downloadUrl), archiveFile, shouldCancel, *this,
                                          downloadError, {}, onBytes))
    {
        const juce::ScopedLock sl (lock);
        if (downloadError == "cancelled")
        {
            resultMessage = "ONNX Runtime download cancelled";
            state.store (StemModelDownloadState::cancelled, std::memory_order_release);
        }
        else
        {
            resultMessage = "Failed to download " + entry.menuLabel + " (" + downloadError + ")";
            state.store (StemModelDownloadState::failed, std::memory_order_release);
        }
        return;
    }

    // 3. Extract.
    {
        const juce::ScopedLock sl (lock);
        statusText = "Installing " + entry.menuLabel;
    }

    juce::String extractError;
    if (! Download::extractZipToFolder (archiveFile, bundleFolder, extractError))
    {
        archiveFile.deleteFile();
        const juce::ScopedLock sl (lock);
        resultMessage = "Failed to install " + entry.menuLabel + ": " + extractError;
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
    }

    archiveFile.deleteFile();

    // 4. Activate this bundle.
    if (! writeActiveOrtBundleDirectoryName (rootFolder, entry.directoryName))
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Failed to activate " + entry.menuLabel;
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
    }

    progress.store (1.0f, std::memory_order_release);

    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Installed " + entry.menuLabel + " (restart INTERSECT to use it)";
        statusText = {};
    }
    state.store (StemModelDownloadState::completed, std::memory_order_release);
}
