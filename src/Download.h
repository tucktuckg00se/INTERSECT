#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

// File downloads and archive extraction shared by the stem-model, ONNX Runtime and URL-import
// jobs. All blocking: worker threads only.
namespace Download
{
/** Downloads url into targetFile through a temporary file, so a failed or cancelled download never
    leaves a partial target behind. When expectedSha256 is set the file is only installed if its
    hash matches. onBytes (received, total) reports progress; total is -1 when the server doesn't
    say. file:// URLs work too (local testing).

    errorMessage is one of: "cancelled", "HTTP <code>", "network connection failed",
    "write failed", "checksum mismatch", "install failed". */
bool downloadFileToTarget (const juce::URL& url,
                           const juce::File& targetFile,
                           std::atomic<bool>& shouldCancel,
                           juce::Thread& thread,
                           juce::String& errorMessage,
                           const juce::String& expectedSha256 = {},
                           const std::function<void (juce::int64, juce::int64)>& onBytes = {});

/** Unzips archive into destination (created if needed). juce::ZipFile restores symlinks but not
    Unix permissions: set the exec bit on executables afterwards. */
bool extractZipToFolder (const juce::File& archive, const juce::File& destination, juce::String& errorMessage);

/** Lower-case hex SHA-256 of a file, or empty if it can't be read. */
juce::String sha256OfFile (const juce::File& file);

/** Text from an https:// or file:// URL; empty on any failure. Unlike fetchText it reads local
    files directly (they have no HTTP status). Adds a User-Agent for GitHub. */
juce::String fetchTextOrFile (const juce::URL& url, juce::Thread& thread, int timeoutMs = 15000);
}
