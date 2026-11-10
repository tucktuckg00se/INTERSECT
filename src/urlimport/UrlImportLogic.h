#pragma once
#include <juce_core/juce_core.h>

/**
    Import from URL: the pure parts (no files, network or processes), so they can be unit-tested.

    yt-dlp does the downloading. It is never built or shipped by INTERSECT: it comes from yt-dlp's
    own GitHub releases (the onedir zips, which run as a single process and start fast) and is
    checked against the release's SHA2-256SUMS. ffmpeg (audio to WAV) and qjs (the JavaScript
    runtime yt-dlp needs for YouTube) come from INTERSECT's tools repo, pinned by a manifest with
    a sha256 per platform.
*/
namespace UrlImport
{
enum class Channel
{
    stable,    // yt-dlp's tested releases, roughly monthly
    nightly,   // yt-dlp's nightly builds: site fixes arrive within a day
};

juce::String channelToString (Channel channel);
Channel channelFromString (const juce::String& text);   // anything unknown is stable

constexpr const char* kMediaManifestAssetName = "intersect_media_tools_manifest.json";
constexpr const char* kToolsReleasesApiUrl = "https://api.github.com/repos/tucktuckg00se/intersect-ort-providers/releases?per_page=30";
/** Test hook: a URL (https:// or file://) to read the media manifest from instead of the newest
    media-* release. Lets a build be tried with locally built tools before anything is published. */
constexpr const char* kTestManifestEnvVar = "INTERSECT_MEDIA_TOOLS_MANIFEST";
/** Longer than this asks before importing (the sample is held in memory as float). */
constexpr double kLongImportSeconds = 3600.0;

//==============================================================================
// yt-dlp releases

/** The onedir zip for a platform tag (see getCurrentPlatformTag()), or empty if unsupported. */
juce::String ytDlpAssetName (const juce::String& platformTag);
/** Base URL that SHA2-256SUMS and the zips are downloaded from (ends with '/'). */
juce::String ytDlpDownloadBaseUrl (Channel channel);
/** The checksum for assetName from a SHA2-256SUMS file ("<hex>  <name>" or "<hex> *<name>" lines),
    lower-case, or empty if it isn't listed. */
juce::String findSha256 (const juce::String& sums, const juce::String& assetName);

//==============================================================================
// Media tools (ffmpeg + qjs) from the tools repo

struct MediaToolsEntry
{
    juce::String release;         // e.g. media-9.0.2-0.18.0
    juce::String ffmpegVersion;
    juce::String qjsVersion;
    juce::String downloadUrl;
    juce::String sha256;
    juce::int64 bytes = 0;
    juce::String ffmpegFile;      // file names inside the (flat) zip
    juce::String qjsFile;

    bool isValid() const { return downloadUrl.isNotEmpty() && sha256.length() == 64; }
};

/** The newest published media-* release that carries the manifest (the API lists newest first),
    as the manifest's download URL; empty if none. */
juce::String pickMediaManifestUrl (const juce::var& releases);
/** The entry for platformTag from a schema-1 manifest; invalid (with error set) otherwise. */
MediaToolsEntry parseMediaToolsManifest (const juce::String& json, const juce::String& platformTag, juce::String& error);

//==============================================================================
// Running yt-dlp

/** A pasted or typed link worth handing to yt-dlp: http(s), a host, no spaces, sane length. */
bool isWebUrl (const juce::String& text);

struct YtDlpPaths
{
    juce::File ffmpeg;
    juce::File qjs;
    juce::File cacheDir;
    juce::File workDir;
};

/** yt-dlp's arguments (without the executable). allowLong drops the length limit after the user
    confirmed; live streams are always refused. The URL comes after "--" so it can't be read as an
    option. */
juce::StringArray buildYtDlpArgs (const YtDlpPaths& paths, const juce::String& url, bool allowLong);

/** One line of yt-dlp's output, recognised by the [ix-...] markers our templates print. */
struct OutputLine
{
    enum class Kind
    {
        other,
        download,      // [ix-dl] progress
        postprocess,   // [ix-pp] a post-processor started or finished
        info,          // [ix-info] before the filters: title, duration, live status
        done,          // [ix-done] the final file is in place
        error,         // ERROR: ...
        warning,       // WARNING: ...
    };

    Kind kind = Kind::other;
    juce::String status;          // download / postprocess status ("downloading", "started", ...)
    juce::int64 downloadedBytes = -1;
    juce::int64 totalBytes = -1;  // or the estimate when the size isn't known
    int fragmentIndex = -1;
    int fragmentCount = -1;
    juce::String postprocessor;
    juce::String id;
    juce::String title;
    double duration = -1.0;
    bool isLive = false;          // live now or not started yet
    juce::String filepath;
    juce::String text;            // error / warning message, without the prefix
};

OutputLine parseOutputLine (const juce::String& line);
/** 0-1 for a download line, or -1 when nothing tells how far along it is. */
float downloadFraction (const OutputLine& line);

enum class FailureKind
{
    none,
    unsupportedUrl,
    needsAccount,    // age-restricted, members-only, private
    botCheck,        // "Sign in to confirm you're not a bot"
    geoBlocked,
    unavailable,     // removed, deleted, wrong link
    noAudio,         // nothing INTERSECT could take
    network,
    ytDlpOutdated,   // the site changed; a newer yt-dlp usually fixes it
    other,
};

/** Reads the ERROR lines (and context) of a failed run. */
FailureKind classifyFailure (const juce::StringArray& outputTail);
/** The message shown to the user. rawError is yt-dlp's own ERROR text, used for `other`. */
juce::String describeFailure (FailureKind kind, const juce::String& rawError);
/** True when checking for a newer yt-dlp and retrying is worth it. */
bool failureSuggestsUpdate (FailureKind kind);

/** "<title>.wav" made safe for every OS: illegal characters, reserved Windows names, trailing dots
    and spaces, length. Falls back to the id, then "Imported audio". */
juce::String makeOutputFileName (const juce::String& title, const juce::String& id);
/** folder/fileName, or "name (2).ext", "name (3).ext"... if taken. (juce::File's own numbering
    misreads titles that already end in brackets.) */
juce::File uniqueFileIn (const juce::File& folder, const juce::String& fileName);
} // namespace UrlImport
