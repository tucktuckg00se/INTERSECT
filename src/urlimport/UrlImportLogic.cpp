#include "UrlImportLogic.h"

namespace UrlImport
{
namespace
{
bool isHex64 (const juce::String& text)
{
    return text.length() == 64 && text.containsOnly ("0123456789abcdefABCDEF");
}

/** A bare file name from a manifest: no folders, nothing that could step outside the tools folder. */
bool isPlainFileName (const juce::String& name)
{
    return name.isNotEmpty() && ! name.containsAnyOf ("/\\:") && name != "." && name != "..";
}

juce::var parseJsonObject (const juce::String& text)
{
    const auto parsed = juce::JSON::parse (text);
    return parsed.getDynamicObject() != nullptr ? parsed : juce::var();
}

juce::int64 int64Property (const juce::var& object, const char* name)
{
    const auto value = object[name];
    if (value.isInt() || value.isInt64() || value.isDouble())
        return (juce::int64) value;
    return -1;
}

bool containsAny (const juce::String& text, std::initializer_list<const char*> needles)
{
    for (const auto* needle : needles)
        if (text.contains (needle))
            return true;
    return false;
}
} // namespace

juce::String channelToString (Channel channel)
{
    return channel == Channel::nightly ? "nightly" : "stable";
}

Channel channelFromString (const juce::String& text)
{
    return text.trim().equalsIgnoreCase ("nightly") ? Channel::nightly : Channel::stable;
}

//==============================================================================

juce::String ytDlpAssetName (const juce::String& platformTag)
{
    if (platformTag == "linux-x64")   return "yt-dlp_linux.zip";
    if (platformTag == "linux-arm64") return "yt-dlp_linux_aarch64.zip";
    if (platformTag == "macos-arm64" || platformTag == "macos-x64") return "yt-dlp_macos.zip";   // universal
    if (platformTag == "win-x64")     return "yt-dlp_win.zip";
    if (platformTag == "win-arm64")   return "yt-dlp_win_arm64.zip";
    return {};
}

juce::String ytDlpDownloadBaseUrl (Channel channel)
{
    return channel == Channel::nightly
             ? "https://github.com/yt-dlp/yt-dlp-nightly-builds/releases/latest/download/"
             : "https://github.com/yt-dlp/yt-dlp/releases/latest/download/";
}

juce::String findSha256 (const juce::String& sums, const juce::String& assetName)
{
    if (assetName.isEmpty())
        return {};

    for (const auto& rawLine : juce::StringArray::fromLines (sums))
    {
        const auto line = rawLine.trim();
        const auto hex = line.upToFirstOccurrenceOf (" ", false, false);
        auto name = line.fromFirstOccurrenceOf (" ", false, false).trim();
        if (name.startsWithChar ('*'))
            name = name.substring (1);   // "binary mode" marker of sha256sum
        if (isHex64 (hex) && name == assetName)
            return hex.toLowerCase();
    }
    return {};
}

//==============================================================================

juce::String pickMediaManifestUrl (const juce::var& releases)
{
    const auto* list = releases.getArray();
    if (list == nullptr)
        return {};

    for (const auto& release : *list)
    {
        if ((bool) release["draft"] || (bool) release["prerelease"])
            continue;
        if (! release["tag_name"].toString().trim().startsWith ("media-"))
            continue;

        if (const auto* assets = release["assets"].getArray())
            for (const auto& asset : *assets)
                if (asset["name"].toString() == kMediaManifestAssetName)
                {
                    const auto url = asset["browser_download_url"].toString().trim();
                    if (url.startsWith ("https://"))
                        return url;
                }
    }
    return {};
}

MediaToolsEntry parseMediaToolsManifest (const juce::String& json, const juce::String& platformTag, juce::String& error)
{
    const auto root = parseJsonObject (json);
    if (root.isVoid())
    {
        error = "the tools manifest isn't valid JSON";
        return {};
    }
    if ((int) root["schema"] != 1)
    {
        error = "this INTERSECT can't read tools manifest schema " + root["schema"].toString() + "; update INTERSECT";
        return {};
    }

    const auto platform = root["platforms"][juce::Identifier (platformTag.isNotEmpty() ? platformTag : "unknown")];
    if (platform.getDynamicObject() == nullptr)
    {
        error = "no audio tools are published for " + (platformTag.isNotEmpty() ? platformTag : juce::String ("this platform"));
        return {};
    }

    const bool windows = platformTag.startsWith ("win-");
    MediaToolsEntry entry;
    entry.release = root["release"].toString();
    entry.ffmpegVersion = root["ffmpeg"].toString();
    entry.qjsVersion = root["qjs"].toString();
    entry.downloadUrl = platform["download_url"].toString().trim();
    entry.sha256 = platform["sha256"].toString().trim().toLowerCase();
    entry.bytes = int64Property (platform, "bytes");
    entry.ffmpegFile = platform["ffmpeg"].toString().trim();
    entry.qjsFile = platform["qjs"].toString().trim();
    if (entry.ffmpegFile.isEmpty())
        entry.ffmpegFile = windows ? "ffmpeg.exe" : "ffmpeg";
    if (entry.qjsFile.isEmpty())
        entry.qjsFile = windows ? "qjs.exe" : "qjs";

    const bool urlOk = entry.downloadUrl.startsWith ("https://") || entry.downloadUrl.startsWith ("file:");
    if (! urlOk || ! isHex64 (entry.sha256) || ! isPlainFileName (entry.ffmpegFile) || ! isPlainFileName (entry.qjsFile))
    {
        error = "the tools manifest entry for " + platformTag + " is incomplete";
        return {};
    }
    return entry;
}

//==============================================================================

bool isWebUrl (const juce::String& text)
{
    const auto url = text.trim();
    if (url.length() < 10 || url.length() > 2048 || url.containsAnyOf (" \t\r\n"))
        return false;

    const auto lower = url.toLowerCase();
    if (! lower.startsWith ("https://") && ! lower.startsWith ("http://"))
        return false;

    const auto host = url.fromFirstOccurrenceOf ("://", false, false).upToFirstOccurrenceOf ("/", false, false)
                          .upToFirstOccurrenceOf ("?", false, false).upToFirstOccurrenceOf ("#", false, false);
    return host.contains (".") && ! host.startsWithChar ('.') && ! host.endsWithChar ('.');
}

juce::StringArray buildYtDlpArgs (const YtDlpPaths& paths, const juce::String& url, bool allowLong)
{
    juce::StringArray args {
        // Only what we pass counts: no user config files, no plugins, no other JS runtime
        // (yt-dlp would prefer an installed deno or node over our qjs).
        "--ignore-config", "--no-plugin-dirs",
        "--no-js-runtimes", "--js-runtimes", "quickjs:" + paths.qjs.getFullPathName(),
        "--no-playlist",
        // --print switches on quiet mode, so progress has to be asked for.
        "--quiet", "--progress", "--newline", "--color", "never",
        "--cache-dir", paths.cacheDir.getFullPathName(),
        "--socket-timeout", "30",
        "-f", "bestaudio/best",
        "-x", "--audio-format", "wav",
        "--ppa", "ExtractAudio+ffmpeg_o:-c:a pcm_f32le",
        // The executable itself: a folder makes yt-dlp look for files named exactly ffmpeg/ffprobe.
        "--ffmpeg-location", paths.ffmpeg.getFullPathName(),
        "--fixup", "never",
        "--no-mtime",
        // Exactly one filter: several are ORed.
        "--match-filters", allowLong ? "!is_live" : "!is_live & duration<=?" + juce::String ((int) kLongImportSeconds),
        "-P", paths.workDir.getFullPathName(),
        "-o", "dl.%(ext)s",
        "--progress-template",
        "download:[ix-dl] %(progress.{status,downloaded_bytes,total_bytes,total_bytes_estimate,fragment_index,fragment_count})j",
        "--progress-template", "postprocess:[ix-pp] %(progress.{status,postprocessor})j",
        // pre_process prints before the filters, so a rejected video still says why.
        "--print", "pre_process:[ix-info] %(.{id,title,duration,is_live,live_status})j",
        "--print", "after_move:[ix-done] %(.{id,title,duration,extractor_key,filepath})j",
    };
    args.add ("--");
    args.add (url.trim());
    return args;
}

OutputLine parseOutputLine (const juce::String& rawLine)
{
    const auto line = rawLine.trimEnd();
    OutputLine out;

    auto jsonAfter = [&line] (const char* marker) { return parseJsonObject (line.fromFirstOccurrenceOf (marker, false, false)); };

    if (line.startsWith ("[ix-dl] "))
    {
        const auto json = jsonAfter ("[ix-dl] ");
        if (json.isVoid())
            return out;
        out.kind = OutputLine::Kind::download;
        out.status = json["status"].toString();
        out.downloadedBytes = int64Property (json, "downloaded_bytes");
        out.totalBytes = int64Property (json, "total_bytes");
        if (out.totalBytes <= 0)
            out.totalBytes = int64Property (json, "total_bytes_estimate");
        out.fragmentIndex = (int) int64Property (json, "fragment_index");
        out.fragmentCount = (int) int64Property (json, "fragment_count");
    }
    else if (line.startsWith ("[ix-pp] "))
    {
        const auto json = jsonAfter ("[ix-pp] ");
        if (json.isVoid())
            return out;
        out.kind = OutputLine::Kind::postprocess;
        out.status = json["status"].toString();
        out.postprocessor = json["postprocessor"].toString();
    }
    else if (line.startsWith ("[ix-info] ") || line.startsWith ("[ix-done] "))
    {
        const bool done = line.startsWith ("[ix-done] ");
        const auto json = jsonAfter (done ? "[ix-done] " : "[ix-info] ");
        if (json.isVoid())
            return out;
        out.kind = done ? OutputLine::Kind::done : OutputLine::Kind::info;
        out.id = json["id"].isVoid() ? juce::String() : json["id"].toString();
        out.title = json["title"].isVoid() ? juce::String() : json["title"].toString();
        const auto duration = json["duration"];
        out.duration = (duration.isInt() || duration.isInt64() || duration.isDouble()) ? (double) duration : -1.0;
        const auto liveStatus = json["live_status"].toString();
        out.isLive = (bool) json["is_live"] || liveStatus == "is_live" || liveStatus == "is_upcoming";
        out.filepath = json["filepath"].isVoid() ? juce::String() : json["filepath"].toString();
    }
    else if (line.startsWith ("ERROR:"))
    {
        out.kind = OutputLine::Kind::error;
        out.text = line.substring (6).trim();
    }
    else if (line.startsWith ("WARNING:"))
    {
        out.kind = OutputLine::Kind::warning;
        out.text = line.substring (8).trim();
    }
    return out;
}

float downloadFraction (const OutputLine& line)
{
    if (line.kind != OutputLine::Kind::download)
        return -1.0f;
    if (line.status == "finished")
        return 1.0f;
    if (line.totalBytes > 0 && line.downloadedBytes >= 0)
        return juce::jlimit (0.0f, 1.0f, (float) ((double) line.downloadedBytes / (double) line.totalBytes));
    if (line.fragmentCount > 0 && line.fragmentIndex >= 0)
        return juce::jlimit (0.0f, 1.0f, (float) line.fragmentIndex / (float) line.fragmentCount);
    return -1.0f;
}

//==============================================================================

FailureKind classifyFailure (const juce::StringArray& outputTail)
{
    juce::String errors;
    for (const auto& line : outputTail)
        if (line.startsWith ("ERROR:"))
            errors << line.toLowerCase() << '\n';
    if (errors.isEmpty())
        return FailureKind::other;

    // Order matters: the bot check also suggests --cookies, like the account cases, and our own
    // tools failing ("ffmpeg not found", "Postprocessing: ...") must not read as a missing video.
    if (containsAny (errors, { "ffmpeg not found", "ffprobe and ffmpeg", "postprocessing:" }))
        return FailureKind::other;
    if (errors.contains ("unsupported url"))
        return FailureKind::unsupportedUrl;
    if (errors.contains ("not a bot"))
        return FailureKind::botCheck;
    if (containsAny (errors, { "confirm your age", "age-restricted", "age restricted", "inappropriate for some users",
                               "members-only", "members only", "join this channel", "private video",
                               "video is private", "requires authentication", "login required", "log in to",
                               "sign in to", "--cookies" }))
        return FailureKind::needsAccount;
    if (containsAny (errors, { "available in your country", "geo restrict", "geo-restrict", "geo-blocked",
                               "not available from your location" }))
        return FailureKind::geoBlocked;
    if (containsAny (errors, { "requested format is not available", "no video formats", "no formats found",
                               "no audio" }))
        return FailureKind::noAudio;
    if (containsAny (errors, { "nsig", "signature", "please report this issue", "http error 403", "unable to extract",
                               "js challenge", "challenge solving", "player response" }))
        return FailureKind::ytDlpOutdated;
    if (containsAny (errors, { "video unavailable", "video is unavailable", "has been removed", "no longer available",
                               "does not exist", "http error 404", "not found" }))
        return FailureKind::unavailable;
    if (containsAny (errors, { "unable to download webpage", "urlopen error", "timed out", "timeout",
                               "name or service not known", "getaddrinfo", "network is unreachable",
                               "temporary failure in name resolution", "connection refused", "connection reset",
                               "ssl", "failed to resolve", "no route to host" }))
        return FailureKind::network;
    return FailureKind::other;
}

juce::String describeFailure (FailureKind kind, const juce::String& rawError)
{
    switch (kind)
    {
        case FailureKind::none:           return {};
        case FailureKind::unsupportedUrl: return "That link isn't one yt-dlp can import from.";
        case FailureKind::needsAccount:   return "This needs a signed-in account (age-restricted, members-only or private), which INTERSECT doesn't support yet.";
        case FailureKind::botCheck:       return "The site wants to confirm you're not a bot. This is common on VPNs and shared networks: try without a VPN, wait a while, or switch URL Import to Nightly in Settings.";
        case FailureKind::geoBlocked:     return "This isn't available in your country.";
        case FailureKind::unavailable:    return "That video or track is unavailable (removed, private or a wrong link).";
        case FailureKind::noAudio:        return "No audio could be found at that link.";
        case FailureKind::network:        return "Couldn't reach the site. Check your internet connection and try again.";
        case FailureKind::ytDlpOutdated:  return "The site changed and yt-dlp can't read it yet. A fix usually arrives within days; switching URL Import to Nightly in Settings often helps sooner.";
        case FailureKind::other:          break;
    }
    return rawError.isNotEmpty() ? "yt-dlp: " + rawError : "The import failed. COPY DETAILS has yt-dlp's output.";
}

bool failureSuggestsUpdate (FailureKind kind)
{
    return kind == FailureKind::ytDlpOutdated || kind == FailureKind::other;
}

//==============================================================================

juce::String makeOutputFileName (const juce::String& title, const juce::String& id)
{
    auto base = juce::File::createLegalFileName (title.trim());
    if (base.trim().isEmpty())
        base = juce::File::createLegalFileName (id.trim());
    if (base.trim().isEmpty())
        base = "Imported audio";

    // Keep room for " (12).wav" and deep folders; cut on a character, not a byte.
    constexpr int kMaxChars = 120;
    if (base.length() > kMaxChars)
        base = base.substring (0, kMaxChars);

    // Windows refuses names ending in a dot or space.
    while (base.endsWithChar ('.') || base.endsWithChar (' '))
        base = base.dropLastCharacters (1);
    if (base.isEmpty())
        base = "Imported audio";

    // Reserved device names (CON, NUL, COM1, ...) are invalid on Windows even with an extension.
    static const juce::StringArray reserved { "CON", "PRN", "AUX", "NUL",
                                              "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                              "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };
    if (reserved.contains (base.upToFirstOccurrenceOf (".", false, false).trim(), true))
        base = "_" + base;

    return base + ".wav";
}

juce::File uniqueFileIn (const juce::File& folder, const juce::String& fileName)
{
    const auto candidate = folder.getChildFile (fileName);
    if (! candidate.exists())
        return candidate;

    const auto stem = fileName.upToLastOccurrenceOf (".", false, false);
    const auto extension = fileName.fromLastOccurrenceOf (".", true, false);
    for (int number = 2; number < 10000; ++number)
    {
        const auto numbered = folder.getChildFile (stem + " (" + juce::String (number) + ")" + extension);
        if (! numbered.exists())
            return numbered;
    }
    return folder.getNonexistentChildFile (stem, extension, true);
}
} // namespace UrlImport
