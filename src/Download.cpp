#include "Download.h"
#include "HttpFetch.h"
#include <juce_cryptography/juce_cryptography.h>

namespace Download
{

bool downloadFileToTarget (const juce::URL& url,
                           const juce::File& targetFile,
                           std::atomic<bool>& shouldCancel,
                           juce::Thread& thread,
                           juce::String& errorMessage,
                           const juce::String& expectedSha256,
                           const std::function<void (juce::int64, juce::int64)>& onBytes)
{
    juce::TemporaryFile tempFile (targetFile);
    int statusCode = 0;
    juce::StringPairArray responseHeaders;
    auto input = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (30000)
            .withNumRedirectsToFollow (5)
            .withExtraHeaders ("User-Agent: INTERSECT")
            .withStatusCode (&statusCode)
            .withResponseHeaders (&responseHeaders));

    if (input == nullptr || statusCode >= 400)
    {
        errorMessage = statusCode > 0
                         ? "HTTP " + juce::String (statusCode)
                         : "network connection failed";
        return false;
    }

    auto output = tempFile.getFile().createOutputStream();
    if (output == nullptr)
    {
        errorMessage = "write failed";
        return false;
    }

    const auto totalBytes = input->getTotalLength();
    juce::int64 receivedBytes = 0;
    juce::HeapBlock<char> buffer (64 * 1024);
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

        if (! output->write (buffer.getData(), (size_t) bytesRead))
        {
            output.reset();
            tempFile.deleteTemporaryFile();
            errorMessage = "write failed";
            return false;
        }

        receivedBytes += bytesRead;
        if (onBytes)
            onBytes (receivedBytes, totalBytes);
    }

    output->flush();
    const bool writeOk = output->getStatus().wasOk();
    output.reset();
    if (! writeOk)
    {
        tempFile.deleteTemporaryFile();
        errorMessage = "write failed";
        return false;
    }

    if (expectedSha256.isNotEmpty() && ! sha256OfFile (tempFile.getFile()).equalsIgnoreCase (expectedSha256))
    {
        tempFile.deleteTemporaryFile();
        errorMessage = "checksum mismatch";
        return false;
    }

    if (! tempFile.overwriteTargetFileWithTemporary())
    {
        tempFile.deleteTemporaryFile();
        errorMessage = "install failed";
        return false;
    }

    return true;
}

bool extractZipToFolder (const juce::File& archive, const juce::File& destination, juce::String& errorMessage)
{
    juce::ZipFile zip (archive);
    if (zip.getNumEntries() <= 0)
    {
        errorMessage = "archive is empty or not a valid zip";
        return false;
    }

    if (const auto created = destination.createDirectory(); created.failed())
    {
        errorMessage = "couldn't create " + destination.getFullPathName() + ": " + created.getErrorMessage();
        return false;
    }

    const auto result = zip.uncompressTo (destination, true);
    if (result.failed())
    {
        errorMessage = "extraction failed: " + result.getErrorMessage();
        return false;
    }

    return true;
}

juce::String sha256OfFile (const juce::File& file)
{
    juce::FileInputStream input (file);
    return input.openedOk() ? juce::SHA256 (input).toHexString().toLowerCase() : juce::String();
}

juce::String fetchTextOrFile (const juce::URL& url, juce::Thread& thread, int timeoutMs)
{
    if (url.isLocalFile())
    {
        const auto file = url.getLocalFile();
        return file.existsAsFile() ? file.loadFileAsString() : juce::String();
    }

    // GitHub's API and release downloads reject requests without a User-Agent.
    return fetchText (url, thread, "User-Agent: INTERSECT\r\nAccept: application/vnd.github+json", timeoutMs);
}

} // namespace Download
