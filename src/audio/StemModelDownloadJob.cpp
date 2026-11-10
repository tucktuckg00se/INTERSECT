#include "StemModelDownloadJob.h"
#include "../HttpFetch.h"
#include "../Download.h"

juce::String resolveStemModelManifestUrl (juce::Thread& thread)
{
    // GitHub's API rejects requests without a User-Agent.
    const auto releases = fetchText (juce::URL (getStemModelReleasesApiUrl()), thread,
                                     "User-Agent: INTERSECT\r\nAccept: application/vnd.github+json");
    const auto latest = releases.isNotEmpty()
                          ? pickLatestSeriesManifestUrl (juce::JSON::parse (releases), getStemModelReleaseSeries(),
                                                         getStemModelManifestAssetName())
                          : juce::String();
    return latest.isNotEmpty() ? latest : getStemModelManifestDownloadUrl();
}

bool downloadStemModelManifest (const juce::File& modelFolder, juce::Thread& thread,
                                std::atomic<bool>& shouldCancel, juce::String& errorMessage)
{
    const auto text = fetchText (juce::URL (resolveStemModelManifestUrl (thread)), thread);
    if (thread.threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
    {
        errorMessage = "cancelled";
        return false;
    }

    // Only replace the cache with a manifest this build can read; a bad download keeps the old one.
    const auto parsed = juce::JSON::parse (text);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr || root->getProperty ("models").getArray() == nullptr)
    {
        errorMessage = text.isEmpty() ? "network connection failed" : "invalid manifest";
        return false;
    }

    // TemporaryFile picks a unique name, so a refresh and a model download can't collide.
    const auto manifestFile = getStemModelManifestFile (modelFolder);
    juce::TemporaryFile temp (manifestFile);
    if (! temp.getFile().replaceWithText (text) || ! temp.overwriteTargetFileWithTemporary())
    {
        errorMessage = "install failed";
        return false;
    }

    return true;
}

StemManifestRefreshJob::StemManifestRefreshJob()
    : juce::Thread ("StemManifestRefresh")
{
}

StemManifestRefreshJob::~StemManifestRefreshJob()
{
    shouldCancel.store (true, std::memory_order_release);
    stopThread (25000);   // two 10 s connects at worst
}

bool StemManifestRefreshJob::start (juce::File modelFolder)
{
    if (isThreadRunning())
        return false;

    folder = std::move (modelFolder);
    shouldCancel.store (false, std::memory_order_release);
    startThread (juce::Thread::Priority::background);
    return true;
}

void StemManifestRefreshJob::run()
{
    if (! folder.createDirectory().wasOk())
        return;

    const auto manifestFile = getStemModelManifestFile (folder);
    const auto before = manifestFile.existsAsFile() ? manifestFile.loadFileAsString() : juce::String();

    juce::String error;
    if (! downloadStemModelManifest (folder, *this, shouldCancel, error))
    {
        // Offline or GitHub unreachable: keep whatever is cached; the built-in entries cover the rest.
        juce::Logger::writeToLog ("Stem model manifest refresh failed: " + error);
        return;
    }

    if (onManifestChanged && manifestFile.loadFileAsString() != before)
        onManifestChanged();
}

StemModelDownloadJob::StemModelDownloadJob()
    : juce::Thread ("StemModelDownload")
{
}

StemModelDownloadJob::~StemModelDownloadJob()
{
    shouldCancel.store (true, std::memory_order_release);
    stopThread (10000);
}

bool StemModelDownloadJob::start (juce::File modelFolder, std::vector<StemModelId> modelIds)
{
    if (isThreadRunning() || modelIds.empty())
        return false;

    downloadFolder = std::move (modelFolder);
    requestedModels = std::move (modelIds);
    progress.store (0.0f, std::memory_order_release);
    shouldCancel.store (false, std::memory_order_release);

    {
        const juce::ScopedLock sl (lock);
        statusText = {};
        resultMessage = {};
        currentModelId = requestedModels.front();
    }

    state.store (StemModelDownloadState::downloading, std::memory_order_release);
    startThread (juce::Thread::Priority::background);
    return true;
}

void StemModelDownloadJob::cancel()
{
    shouldCancel.store (true, std::memory_order_release);
    signalThreadShouldExit();
}

juce::String StemModelDownloadJob::getStatusText() const
{
    const juce::ScopedLock sl (lock);
    return statusText;
}

StemModelId StemModelDownloadJob::getCurrentModelId() const
{
    const juce::ScopedLock sl (lock);
    return currentModelId;
}

juce::String StemModelDownloadJob::consumeResultMessage()
{
    const juce::ScopedLock sl (lock);
    auto message = resultMessage;
    resultMessage = {};
    statusText = {};
    progress.store (0.0f, std::memory_order_release);
    currentModelId = {};
    state.store (StemModelDownloadState::idle, std::memory_order_release);
    return message;
}

void StemModelDownloadJob::run()
{
    // Fire onTerminalState when run() returns, regardless of which terminal
    // branch wrote the state. Lets the owner (PluginProcessor) schedule its
    // message-thread drain without the audio thread having to poll.
    struct TerminalNotifier
    {
        std::function<void()>& cb;
        ~TerminalNotifier() { if (cb) cb(); }
    } notify { onTerminalState };

    if (const auto createResult = downloadFolder.createDirectory(); createResult.failed())
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Failed to create model folder: " + createResult.getErrorMessage();
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
    }

    {
        juce::String downloadError;
        if (! downloadStemModelManifest (downloadFolder, *this, shouldCancel, downloadError))
        {
            if (downloadError == "cancelled")
            {
                const juce::ScopedLock sl (lock);
                resultMessage = "Model download cancelled";
                state.store (StemModelDownloadState::cancelled, std::memory_order_release);
                return;
            }

            // Manifest is optional fallback metadata. Keep going on failure.
        }
    }

    for (size_t i = 0; i < requestedModels.size(); ++i)
    {
        if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
        {
            const juce::ScopedLock sl (lock);
            resultMessage = "Model download cancelled";
            state.store (StemModelDownloadState::cancelled, std::memory_order_release);
            return;
        }

        const auto modelId = requestedModels[i];
        const auto entry = getEffectiveStemModelCatalogEntry (modelId, downloadFolder);

        {
            const juce::ScopedLock sl (lock);
            currentModelId = modelId;
            statusText = "Downloading " + entry.menuLabel;
        }

        if (! entry.isValid())
        {
            const juce::ScopedLock sl (lock);
            resultMessage = "Unknown stem model: " + modelId;
            state.store (StemModelDownloadState::failed, std::memory_order_release);
            return;
        }

        const auto targetFile = downloadFolder.getChildFile (entry.fileName);
        if (entry.downloadUrl.isEmpty())
        {
            const juce::ScopedLock sl (lock);
            resultMessage = "Model downloads are not configured in this build";
            state.store (StemModelDownloadState::failed, std::memory_order_release);
            return;
        }

        juce::String downloadError;
        const auto numModels = (float) requestedModels.size();
        const auto onBytes = [this, i, numModels, expectedBytes = entry.downloadBytes] (juce::int64 received, juce::int64 total)
        {
            if (total <= 0)
                total = expectedBytes;
            const float fileFraction = total > 0 ? juce::jlimit (0.0f, 1.0f, (float) ((double) received / (double) total)) : 0.0f;
            progress.store (((float) i + fileFraction) / numModels, std::memory_order_release);
        };

        if (! Download::downloadFileToTarget (juce::URL (entry.downloadUrl), targetFile, shouldCancel, *this,
                                              downloadError, entry.sha256, onBytes))
        {
            const juce::ScopedLock sl (lock);
            if (downloadError == "cancelled")
            {
                resultMessage = "Model download cancelled";
                state.store (StemModelDownloadState::cancelled, std::memory_order_release);
            }
            else if (downloadError.startsWith ("HTTP "))
            {
                resultMessage = "Failed to download " + entry.menuLabel
                              + " (" + downloadError + ")";
                state.store (StemModelDownloadState::failed, std::memory_order_release);
            }
            else if (downloadError == "write failed")
            {
                resultMessage = "Failed to write " + entry.fileName;
                state.store (StemModelDownloadState::failed, std::memory_order_release);
            }
            else if (downloadError == "checksum mismatch")
            {
                resultMessage = "Download of " + entry.menuLabel + " was corrupted (checksum mismatch). Try again.";
                state.store (StemModelDownloadState::failed, std::memory_order_release);
            }
            else if (downloadError == "install failed")
            {
                resultMessage = "Failed to install " + entry.menuLabel;
                state.store (StemModelDownloadState::failed, std::memory_order_release);
            }
            else
            {
                resultMessage = "Failed to download " + entry.menuLabel
                              + " (network connection failed)";
                state.store (StemModelDownloadState::failed, std::memory_order_release);
            }
            return;
        }

        // The new file is verified and installed: earlier versions of this model are no longer needed.
        // A file still open by a running separation can't be deleted; it's simply left behind.
        (void) removeReplacedStemModelFiles (entry, downloadFolder);

        progress.store ((float) (i + 1) / (float) requestedModels.size(), std::memory_order_release);
    }

    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Model download complete";
        statusText = {};
    }
    state.store (StemModelDownloadState::completed, std::memory_order_release);
}
