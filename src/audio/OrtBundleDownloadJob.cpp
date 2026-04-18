#include "OrtBundleDownloadJob.h"

namespace
{

bool downloadFileToTarget (const juce::URL& url,
                           const juce::File& targetFile,
                           std::atomic<bool>& shouldCancel,
                           juce::Thread& thread,
                           std::atomic<float>* progress,
                           juce::int64 expectedBytes,
                           juce::String& errorMessage)
{
    juce::TemporaryFile tempFile (targetFile);
    int statusCode = 0;
    juce::StringPairArray responseHeaders;
    auto input = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (30000)
            .withNumRedirectsToFollow (5)
            .withStatusCode (&statusCode)
            .withResponseHeaders (&responseHeaders));

    if (input == nullptr || statusCode >= 400)
    {
        errorMessage = statusCode > 0
                         ? "HTTP " + juce::String (statusCode)
                         : "network connection failed";
        return false;
    }

    const juce::int64 totalBytes = expectedBytes > 0
                                     ? expectedBytes
                                     : input->getTotalLength();

    auto output = tempFile.getFile().createOutputStream();
    if (output == nullptr)
    {
        errorMessage = "write failed";
        return false;
    }

    juce::HeapBlock<char> buffer (64 * 1024);
    juce::int64 bytesReceived = 0;
    for (;;)
    {
        if (thread.threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
        {
            output.reset();
            tempFile.deleteTemporaryFile();
            errorMessage = "cancelled";
            return false;
        }

        const int bytesRead = input->read (buffer.getData(), 64 * 1024);
        if (bytesRead <= 0)
            break;

        output->write (buffer.getData(), (size_t) bytesRead);
        bytesReceived += bytesRead;

        if (progress != nullptr && totalBytes > 0)
            progress->store (juce::jlimit (0.0f, 0.95f,
                                           (float) ((double) bytesReceived / (double) totalBytes) * 0.95f),
                             std::memory_order_release);
    }

    output->flush();
    output.reset();

    if (! tempFile.overwriteTargetFileWithTemporary())
    {
        tempFile.deleteTemporaryFile();
        errorMessage = "install failed";
        return false;
    }

    return true;
}

bool extractZipToFolder (const juce::File& archive,
                         const juce::File& destination,
                         juce::String& errorMessage)
{
    juce::ZipFile zip (archive);
    if (zip.getNumEntries() <= 0)
    {
        errorMessage = "archive is empty or not a valid zip";
        return false;
    }

    auto result = zip.uncompressTo (destination, true);
    if (result.failed())
    {
        errorMessage = "extraction failed: " + result.getErrorMessage();
        return false;
    }

    return true;
}

} // namespace

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
        (void) downloadFileToTarget (manifestUrl, manifestFile, shouldCancel, *this,
                                     nullptr, 0, downloadError);
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
    if (! downloadFileToTarget (juce::URL (entry.downloadUrl), archiveFile, shouldCancel, *this,
                                &progress, entry.downloadBytes, downloadError))
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
    if (! extractZipToFolder (archiveFile, bundleFolder, extractError))
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
