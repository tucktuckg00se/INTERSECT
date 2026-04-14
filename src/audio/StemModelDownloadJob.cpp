#include "StemModelDownloadJob.h"

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
    currentModelId.store (requestedModels.front(), std::memory_order_release);
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

juce::String StemModelDownloadJob::consumeResultMessage()
{
    const juce::ScopedLock sl (lock);
    auto message = resultMessage;
    resultMessage = {};
    statusText = {};
    progress.store (0.0f, std::memory_order_release);
    currentModelId.store (StemModelId::bsRoformer2stem, std::memory_order_release);
    state.store (StemModelDownloadState::idle, std::memory_order_release);
    return message;
}

void StemModelDownloadJob::run()
{
    if (const auto createResult = downloadFolder.createDirectory(); createResult.failed())
    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Failed to create model folder: " + createResult.getErrorMessage();
        state.store (StemModelDownloadState::failed, std::memory_order_release);
        return;
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
        currentModelId.store (modelId, std::memory_order_release);

        const auto* entry = findStemModelCatalogEntry (modelId);
        if (entry == nullptr)
            continue;

        {
            const juce::ScopedLock sl (lock);
            statusText = "Downloading " + juce::String (entry->menuLabel);
        }

        const auto targetFile = downloadFolder.getChildFile (entry->fileName);
        if (juce::String (entry->downloadUrl).isEmpty())
        {
            const juce::ScopedLock sl (lock);
            resultMessage = "Model downloads are not configured in this build";
            state.store (StemModelDownloadState::failed, std::memory_order_release);
            return;
        }

        juce::TemporaryFile tempFile (targetFile);
        int statusCode = 0;
        juce::StringPairArray responseHeaders;
        auto input = juce::URL (entry->downloadUrl).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (30000)
                .withNumRedirectsToFollow (5)
                .withStatusCode (&statusCode)
                .withResponseHeaders (&responseHeaders));

        if (input == nullptr || statusCode >= 400)
        {
            const juce::ScopedLock sl (lock);
            if (statusCode > 0)
                resultMessage = "Failed to download " + juce::String (entry->menuLabel)
                              + " (HTTP " + juce::String (statusCode) + ")";
            else
                resultMessage = "Failed to download " + juce::String (entry->menuLabel)
                              + " (network connection failed)";
            state.store (StemModelDownloadState::failed, std::memory_order_release);
            return;
        }

        auto output = tempFile.getFile().createOutputStream();
        if (output == nullptr)
        {
            const juce::ScopedLock sl (lock);
            resultMessage = "Failed to write " + juce::String (entry->fileName);
            state.store (StemModelDownloadState::failed, std::memory_order_release);
            return;
        }

        juce::HeapBlock<char> buffer (64 * 1024);
        for (;;)
        {
            if (threadShouldExit() || shouldCancel.load (std::memory_order_acquire))
            {
                output.reset();
                tempFile.deleteTemporaryFile();
                const juce::ScopedLock sl (lock);
                resultMessage = "Model download cancelled";
                state.store (StemModelDownloadState::cancelled, std::memory_order_release);
                return;
            }

            const int bytesRead = input->read (buffer.getData(), 64 * 1024);
            if (bytesRead <= 0)
                break;

            output->write (buffer.getData(), (size_t) bytesRead);
        }

        output->flush();
        output.reset();

        if (! tempFile.overwriteTargetFileWithTemporary())
        {
            tempFile.deleteTemporaryFile();
            const juce::ScopedLock sl (lock);
            resultMessage = "Failed to install " + juce::String (entry->menuLabel);
            state.store (StemModelDownloadState::failed, std::memory_order_release);
            return;
        }

        progress.store ((float) (i + 1) / (float) requestedModels.size(), std::memory_order_release);
    }

    {
        const juce::ScopedLock sl (lock);
        resultMessage = "Model download complete";
        statusText = {};
    }
    state.store (StemModelDownloadState::completed, std::memory_order_release);
}
