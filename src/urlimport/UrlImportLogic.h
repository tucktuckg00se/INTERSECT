#pragma once
#include <juce_core/juce_core.h>

/**
    Import from URL: the pure parts (no files, network or processes), so they can be unit-tested.

    yt-dlp does the downloading. It is never built or shipped by INTERSECT: it comes from yt-dlp's
    own GitHub releases (the onedir zips, which run as a single process and start fast) and is
    checked against the release's SHA2-256SUMS. ffmpeg (audio to WAV or FLAC) and qjs (the JavaScript
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
constexpr const char* kToolsReleasesApiUrl = "https://api.github.com/repos/tucktuckg00se/intersect-tools/releases?per_page=30";
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
// Output format

/** What an import is saved as. The codec and bit depth are one setting (formatToString), the
    sample rate another. ffmpeg does the conversion, in the same step yt-dlp always runs. */
struct OutputFormat
{
    enum class Codec { wav, flac };

    Codec codec = Codec::wav;
    int bitDepth = 32;    // WAV: 16, 24 or 32 (float). FLAC: 16 or 24.
    int sampleRate = 0;   // Hz; 0 keeps the source's rate

    bool operator== (const OutputFormat& other) const
    {
        return codec == other.codec && bitDepth == other.bitDepth && sampleRate == other.sampleRate;
    }
};

/** A custom sample rate must be in this range (0 = keep the source's). */
constexpr int kMinSampleRate = 8000;
constexpr int kMaxSampleRate = 192000;
bool isValidSampleRate (int hz);

/** The codec and bit depth as a settings value: "wav-f32" (the default), "wav-24", "wav-16",
    "flac-24", "flac-16". */
juce::String formatToString (const OutputFormat& format);
/** Codec and bit depth from formatToString's text; anything unknown is WAV 32-bit float. The
    sample rate is left at 0. */
OutputFormat formatFromString (const juce::String& text);
/** "wav" or "flac". */
juce::String fileExtension (const OutputFormat& format);
/** "WAV" or "FLAC", for progress text. */
juce::String codecName (const OutputFormat& format);
/** The arguments yt-dlp hands ffmpeg for the conversion (the ExtractAudio --ppa value). */
juce::String ffmpegOutputArgs (const OutputFormat& format);

//==============================================================================
// Running yt-dlp

/** A pasted or typed link worth handing to yt-dlp: http(s), a host, no spaces, sane length. */
bool isWebUrl (const juce::String& text);
/** A link as people type it into an address bar: the text itself when isWebUrl accepts it, or
    "https://" + text for scheme-less links ("youtu.be/abc", "www.youtube.com/watch?v=..."): a host
    with a dot followed by a path, or a "www." host. Empty for anything else, so file names
    ("kick.wav"), paths and plain words never count. */
juce::String normaliseTypedUrl (const juce::String& text);

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
juce::StringArray buildYtDlpArgs (const YtDlpPaths& paths, const juce::String& url, bool allowLong,
                                  const OutputFormat& format = {});

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
    toolsOutdated,   // the installed ffmpeg can't write the chosen format (FLAC needs newer tools)
    other,
};

/** Reads the ERROR lines (and context) of a failed run. */
FailureKind classifyFailure (const juce::StringArray& outputTail);
/** The message shown to the user. rawError is yt-dlp's own ERROR text, used for `other`. */
juce::String describeFailure (FailureKind kind, const juce::String& rawError);
/** True when checking for a newer yt-dlp and retrying is worth it. */
bool failureSuggestsUpdate (FailureKind kind);

/** "<title>.<extension>" made safe for every OS: illegal characters, reserved Windows names,
    trailing dots and spaces, length. Falls back to the id, then "Imported audio". */
juce::String makeOutputFileName (const juce::String& title, const juce::String& id,
                                 const juce::String& extension = "wav");
/** folder/fileName, or "name (2).ext", "name (3).ext"... if taken. (juce::File's own numbering
    misreads titles that already end in brackets.) */
juce::File uniqueFileIn (const juce::File& folder, const juce::String& fileName);
} // namespace UrlImport
