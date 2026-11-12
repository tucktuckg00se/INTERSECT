#include "urlimport/UrlImportLogic.h"
#include "urlimport/ToolProcess.h"
#include <iostream>
#include <stdexcept>
#include <string>

#if ! JUCE_WINDOWS
 #include <cerrno>
 #include <cstdlib>
 #include <signal.h>
#endif

using namespace UrlImport;

namespace
{
void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void requireEqual (const juce::String& actual, const juce::String& expected, const std::string& message)
{
    if (actual != expected)
        throw std::runtime_error (message + ": got \"" + actual.toStdString() + "\", expected \"" + expected.toStdString() + "\"");
}

const juce::String kShaA = "e51e69e89489f199530f13d32a4342da3ddb2cf34842f364603a066db05afbf3";
const juce::String kShaB = "767ba9dce7052aa196f8b12652f433d76de128ba168966fa84b6394d5e6c6795";

//==============================================================================

void checksumsParse()
{
    const juce::String sums = kShaA + "  yt-dlp\r\n"
                            + kShaB.toUpperCase() + "  yt-dlp_linux.zip\r\n"
                            + "not a checksum line\n"
                            + kShaA + " *yt-dlp_win.zip\n"
                            + "1234  yt-dlp_macos.zip\n";
    requireEqual (findSha256 (sums, "yt-dlp_linux.zip"), kShaB, "CRLF + upper-case hex, returned lower-case");
    requireEqual (findSha256 (sums, "yt-dlp_win.zip"), kShaA, "binary-mode '*' marker");
    requireEqual (findSha256 (sums, "yt-dlp"), kShaA, "exact name, not a prefix of another");
    requireEqual (findSha256 (sums, "yt-dlp_macos.zip"), {}, "short hash ignored");
    requireEqual (findSha256 (sums, "yt-dlp_linux"), {}, "missing asset");
    requireEqual (findSha256 ({}, "yt-dlp_linux.zip"), {}, "empty file");
}

void assetsPerPlatform()
{
    requireEqual (ytDlpAssetName ("linux-x64"), "yt-dlp_linux.zip", "linux-x64");
    requireEqual (ytDlpAssetName ("linux-arm64"), "yt-dlp_linux_aarch64.zip", "linux-arm64");
    requireEqual (ytDlpAssetName ("macos-arm64"), "yt-dlp_macos.zip", "macos-arm64 (universal)");
    requireEqual (ytDlpAssetName ("macos-x64"), "yt-dlp_macos.zip", "macos-x64 (universal)");
    requireEqual (ytDlpAssetName ("win-x64"), "yt-dlp_win.zip", "win-x64");
    requireEqual (ytDlpAssetName ("win-arm64"), "yt-dlp_win_arm64.zip", "win-arm64");
    requireEqual (ytDlpAssetName ("freebsd-x64"), {}, "unsupported");
    require (ytDlpDownloadBaseUrl (Channel::stable).startsWith ("https://github.com/yt-dlp/yt-dlp/releases/latest/download/"), "stable base");
    require (ytDlpDownloadBaseUrl (Channel::nightly).contains ("yt-dlp-nightly-builds"), "nightly base");
    require (channelFromString (channelToString (Channel::nightly)) == Channel::nightly, "channel round trip");
    require (channelFromString ("garbage") == Channel::stable, "unknown channel is stable");
}

void mediaReleaseIsPicked()
{
    const auto releases = juce::JSON::parse (R"([
        { "tag_name": "media-9.1.0-0.19.0", "draft": true, "prerelease": false,
          "assets": [ { "name": "intersect_media_tools_manifest.json", "browser_download_url": "https://example.com/draft.json" } ] },
        { "tag_name": "ort-1.25.0", "draft": false, "prerelease": false,
          "assets": [ { "name": "intersect_media_tools_manifest.json", "browser_download_url": "https://example.com/ort.json" } ] },
        { "tag_name": "media-9.0.3-0.18.0", "draft": false, "prerelease": true,
          "assets": [ { "name": "intersect_media_tools_manifest.json", "browser_download_url": "https://example.com/pre.json" } ] },
        { "tag_name": "media-9.0.2-0.18.0", "draft": false, "prerelease": false,
          "assets": [ { "name": "intersect_media_tools_manifest.json", "browser_download_url": "https://example.com/good.json" } ] },
        { "tag_name": "media-8.0.0-0.17.0", "draft": false, "prerelease": false,
          "assets": [ { "name": "intersect_media_tools_manifest.json", "browser_download_url": "https://example.com/old.json" } ] }
    ])");
    requireEqual (pickMediaManifestUrl (releases), "https://example.com/good.json", "newest published media release");
    requireEqual (pickMediaManifestUrl (juce::JSON::parse (R"({"message":"API rate limit exceeded"})")), {}, "API error");
    requireEqual (pickMediaManifestUrl (juce::JSON::parse (R"([{ "tag_name": "media-1", "assets": [
        { "name": "intersect_media_tools_manifest.json", "browser_download_url": "http://example.com/plain.json" } ] }])")),
                  {}, "plain http refused");
}

juce::String manifestJson (const juce::String& platformEntry, int schema = 1)
{
    return R"({ "schema": )" + juce::String (schema) + R"(, "release": "media-9.0.2-0.18.0", "ffmpeg": "9.0.2", "qjs": "0.18.0",
               "platforms": { )" + platformEntry + " } }";
}

void manifestParses()
{
    juce::String error;
    const auto good = manifestJson (R"("linux-x64": { "download_url": "https://example.com/t.zip", "sha256": ")" + kShaA
                                    + R"(", "bytes": 4741007, "ffmpeg": "ffmpeg", "qjs": "qjs" },
                                       "win-x64": { "download_url": "file:///C:/tools/t.zip", "sha256": ")" + kShaB + R"(", "bytes": 1 })");
    const auto linuxEntry = parseMediaToolsManifest (good, "linux-x64", error);
    require (linuxEntry.isValid(), "valid linuxEntry entry: " + error.toStdString());
    requireEqual (linuxEntry.release, "media-9.0.2-0.18.0", "release");
    requireEqual (linuxEntry.ffmpegVersion, "9.0.2", "ffmpeg version");
    require (linuxEntry.bytes == 4741007, "bytes");
    const auto windowsEntry = parseMediaToolsManifest (good, "win-x64", error);
    require (windowsEntry.isValid(), "file:// URL accepted (test hook)");
    requireEqual (windowsEntry.ffmpegFile, "ffmpeg.exe", "Windows default file names");
    requireEqual (windowsEntry.qjsFile, "qjs.exe", "Windows default qjs name");

    require (! parseMediaToolsManifest (good, "macos-arm64", error).isValid(), "missing platform");
    require (error.contains ("macos-arm64"), "error names the platform");
    require (! parseMediaToolsManifest (manifestJson (R"("linux-x64": {})", 2), "linux-x64", error).isValid(), "schema 2 refused");
    require (! parseMediaToolsManifest ("not json", "linux-x64", error).isValid(), "junk");
    require (! parseMediaToolsManifest (manifestJson (R"("linux-x64": { "download_url": "https://e.com/t.zip", "sha256": "abc" })"),
                                        "linux-x64", error).isValid(), "short sha");
    require (! parseMediaToolsManifest (manifestJson (R"("linux-x64": { "download_url": "https://e.com/t.zip", "sha256": ")" + kShaA
                                                      + R"(", "ffmpeg": "../../bin/sh" })"), "linux-x64", error).isValid(),
             "a file name that leaves the tools folder");
    require (! parseMediaToolsManifest (manifestJson (R"("linux-x64": { "download_url": "ftp://e.com/t.zip", "sha256": ")" + kShaA + R"(" })"),
                                        "linux-x64", error).isValid(), "unknown URL scheme");
}

void webUrlsAreRecognised()
{
    require (isWebUrl ("https://youtu.be/zsdtyIff2PQ"), "short link");
    require (isWebUrl ("  https://www.youtube.com/watch?v=abc&t=10s \n"), "trimmed");
    require (isWebUrl ("http://example.bandcamp.com/track/x"), "plain http");
    require (isWebUrl ("HTTPS://SoundCloud.com/a/b"), "upper-case scheme");
    require (! isWebUrl ("youtube.com/watch?v=abc"), "no scheme");
    require (! isWebUrl ("file:///etc/passwd"), "file URL");
    require (! isWebUrl ("--exec rm"), "an option");
    require (! isWebUrl ("https://exa mple.com/x"), "space inside");
    require (! isWebUrl ("https://localhost/x"), "host without a dot");
    require (! isWebUrl ("https://"), "nothing after the scheme");
    require (! isWebUrl ("https://example.com/" + juce::String::repeatedString ("a", 3000)), "too long");
}

void typedLinksAreNormalised()
{
    requireEqual (normaliseTypedUrl ("https://youtu.be/zsdtyIff2PQ"), "https://youtu.be/zsdtyIff2PQ", "full link unchanged");
    requireEqual (normaliseTypedUrl ("  youtu.be/zsdtyIff2PQ  "), "https://youtu.be/zsdtyIff2PQ", "scheme added");
    requireEqual (normaliseTypedUrl ("www.youtube.com/watch?v=abc"), "https://www.youtube.com/watch?v=abc", "www link");
    requireEqual (normaliseTypedUrl ("www.example.com"), "https://www.example.com", "bare www host");
    requireEqual (normaliseTypedUrl ("soundcloud.com/artist/track"), "https://soundcloud.com/artist/track", "host + path");
    requireEqual (normaliseTypedUrl ("\"youtu.be/x1\""), "https://youtu.be/x1", "quotes from a paste");
    requireEqual (normaliseTypedUrl ("kick.wav"), {}, "a file name is not a link");
    requireEqual (normaliseTypedUrl ("example.com"), {}, "no path, no www");
    requireEqual (normaliseTypedUrl ("/home/me/Samples"), {}, "absolute path");
    requireEqual (normaliseTypedUrl ("C:\\Samples\\Breaks"), {}, "Windows path");
    requireEqual (normaliseTypedUrl ("my folder/x"), {}, "spaces");
    requireEqual (normaliseTypedUrl ("../Samples/x"), {}, "relative path");
    requireEqual (normaliseTypedUrl ("~/Music/x.wav"), {}, "home path");
    requireEqual (normaliseTypedUrl ("file:///etc/passwd"), {}, "other schemes");
    requireEqual (normaliseTypedUrl ({}), {}, "empty");
}

void ytDlpArgumentsAreSafe()
{
    YtDlpPaths paths;
    paths.ffmpeg = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("t/ffmpeg");
    paths.qjs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("t/qjs");
    paths.cacheDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("t/cache");
    paths.workDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("t/work");

    const auto args = buildYtDlpArgs (paths, " https://youtu.be/x ", false);
    requireEqual (args[args.size() - 1], "https://youtu.be/x", "URL last, trimmed");
    requireEqual (args[args.size() - 2], "--", "\"--\" right before the URL");
    require (args.indexOf ("--no-js-runtimes") >= 0 && args.indexOf ("--no-js-runtimes") < args.indexOf ("--js-runtimes"),
             "default runtimes cleared before ours");
    requireEqual (args[args.indexOf ("--js-runtimes") + 1], "quickjs:" + paths.qjs.getFullPathName(), "our qjs");
    requireEqual (args[args.indexOf ("--ffmpeg-location") + 1], paths.ffmpeg.getFullPathName(), "ffmpeg executable, not its folder");
    require (args.indexOf ("--ignore-config") >= 0 && args.indexOf ("--no-playlist") >= 0, "no user config, one video");

    int filters = 0;
    for (const auto& arg : args)
        filters += arg == "--match-filters" ? 1 : 0;
    require (filters == 1, "exactly one --match-filters (several are ORed)");
    requireEqual (args[args.indexOf ("--match-filters") + 1], "!is_live & duration<=?3600", "length + live filter");
    const auto longArgs = buildYtDlpArgs (paths, "https://youtu.be/x", true);
    requireEqual (longArgs[longArgs.indexOf ("--match-filters") + 1], "!is_live", "confirmed long import keeps the live filter");
    require (args.joinIntoString (" ").contains ("pre_process:[ix-info]"), "info printed before the filters");

    // The default format is what every import used before formats existed.
    requireEqual (args[args.indexOf ("--audio-format") + 1], "wav", "WAV by default");
    requireEqual (args[args.indexOf ("--ppa") + 1], "ExtractAudio+ffmpeg_o:-c:a pcm_f32le", "32-bit float by default");

    OutputFormat flac;
    flac.codec = OutputFormat::Codec::flac;
    flac.bitDepth = 24;
    flac.sampleRate = 48000;
    const auto flacArgs = buildYtDlpArgs (paths, "https://youtu.be/x", false, flac);
    requireEqual (flacArgs[flacArgs.indexOf ("--audio-format") + 1], "flac", "FLAC asked of yt-dlp");
    requireEqual (flacArgs[flacArgs.indexOf ("--ppa") + 1],
                  "ExtractAudio+ffmpeg_o:-c:a flac -sample_fmt s32 -bits_per_raw_sample 24 -ar 48000", "FLAC 24-bit at 48 kHz");
}

void outputFormats()
{
    OutputFormat format;
    requireEqual (ffmpegOutputArgs (format), "-c:a pcm_f32le", "WAV float, source rate");
    format.bitDepth = 24;
    requireEqual (ffmpegOutputArgs (format), "-c:a pcm_s24le", "WAV 24-bit");
    format.sampleRate = 44100;
    requireEqual (ffmpegOutputArgs (format), "-c:a pcm_s24le -ar 44100", "WAV 24-bit resampled");
    format.bitDepth = 16;
    requireEqual (ffmpegOutputArgs (format), "-c:a pcm_s16le -af aresample=osr=44100:osf=s16:dither_method=triangular",
                  "16-bit: one resampler call does the rate and the dither");
    format.sampleRate = 0;
    requireEqual (ffmpegOutputArgs (format), "-c:a pcm_s16le -af aresample=osf=s16:dither_method=triangular",
                  "16-bit at the source rate still dithers");
    format.codec = OutputFormat::Codec::flac;
    requireEqual (ffmpegOutputArgs (format), "-c:a flac -sample_fmt s16 -af aresample=osf=s16:dither_method=triangular", "FLAC 16-bit");
    format.sampleRate = 3000;
    require (! ffmpegOutputArgs (format).contains ("3000"), "an out-of-range rate is ignored, not passed to ffmpeg");

    for (const auto* text : { "wav-f32", "wav-24", "wav-16", "flac-24", "flac-16" })
        requireEqual (formatToString (formatFromString (text)), text, std::string ("round trip ") + text);
    require (formatFromString ("mp3-320") == OutputFormat(), "unknown -> WAV 32-bit float");
    require (formatFromString ("") == OutputFormat(), "missing -> WAV 32-bit float");
    require (formatFromString ("FLAC-16").codec == OutputFormat::Codec::flac, "case-insensitive");
    requireEqual (fileExtension (formatFromString ("flac-24")), "flac", "FLAC extension");
    requireEqual (fileExtension (OutputFormat()), "wav", "WAV extension");

    require (isValidSampleRate (8000) && isValidSampleRate (16000) && isValidSampleRate (192000), "rates in range");
    require (! isValidSampleRate (0) && ! isValidSampleRate (7999) && ! isValidSampleRate (192001), "rates out of range");
}

void outputLinesParse()
{
    auto line = parseOutputLine (R"([ix-dl] {"status": "downloading", "downloaded_bytes": 1024, "total_bytes": 4096})");
    require (line.kind == OutputLine::Kind::download && line.downloadedBytes == 1024 && line.totalBytes == 4096, "download line");
    require (std::abs (downloadFraction (line) - 0.25f) < 1.0e-6f, "fraction from bytes");

    line = parseOutputLine (R"([ix-dl] {"status": "downloading", "downloaded_bytes": 500, "total_bytes_estimate": 1000})");
    require (line.totalBytes == 1000, "estimate when the size is unknown");

    line = parseOutputLine (R"([ix-dl] {"status": "downloading", "downloaded_bytes": 5, "fragment_index": 3, "fragment_count": 12})");
    require (std::abs (downloadFraction (line) - 0.25f) < 1.0e-6f, "fraction from fragments");

    line = parseOutputLine (R"([ix-dl] {"status": "downloading", "downloaded_bytes": 5})");
    require (downloadFraction (line) < 0.0f, "unknown progress");
    line = parseOutputLine (R"([ix-dl] {"status": "finished", "downloaded_bytes": 5})");
    require (downloadFraction (line) > 0.999f, "finished is complete");

    line = parseOutputLine (R"([ix-pp] {"status": "started", "postprocessor": "ExtractAudio"})");
    require (line.kind == OutputLine::Kind::postprocess && line.postprocessor == "ExtractAudio" && line.status == "started", "postprocess");

    line = parseOutputLine (R"([ix-info] {"id": "abc", "title": "Live!", "duration": null, "is_live": true, "live_status": "is_live"})");
    require (line.kind == OutputLine::Kind::info && line.isLive && line.duration < 0.0, "live info");
    line = parseOutputLine (R"([ix-info] {"id": "abc", "title": "Soon", "is_live": false, "live_status": "is_upcoming"})");
    require (line.isLive, "upcoming premiere counts as live");
    line = parseOutputLine (R"([ix-info] {"id": "abc", "title": "Long", "duration": 7322.5, "is_live": false, "live_status": "not_live"})");
    require (! line.isLive && std::abs (line.duration - 7322.5) < 1.0e-9, "duration");

    line = parseOutputLine (R"([ix-done] {"id": "zsdtyIff2PQ", "title": "INTERSECT Quick Start Guide \u2014 Free", "duration": 443, "extractor_key": "Youtube", "filepath": "/tmp/x/dl.wav"})");
    require (line.kind == OutputLine::Kind::done, "done line");
    requireEqual (line.title, juce::String (juce::CharPointer_UTF8 ("INTERSECT Quick Start Guide \xe2\x80\x94 Free")), "\\u escapes decoded");
    requireEqual (line.filepath, "/tmp/x/dl.wav", "file path");

    line = parseOutputLine ("ERROR: [youtube] abc: Video unavailable");
    require (line.kind == OutputLine::Kind::error && line.text == "[youtube] abc: Video unavailable", "error text");
    require (parseOutputLine ("WARNING: something").kind == OutputLine::Kind::warning, "warning");
    require (parseOutputLine ("[ix-dl] {broken").kind == OutputLine::Kind::other, "broken JSON is ignored");
    require (parseOutputLine ("[download] 50% of 3MiB").kind == OutputLine::Kind::other, "yt-dlp's own lines");
}

void failuresAreClassified()
{
    auto kindOf = [] (const char* error) { return classifyFailure (juce::StringArray { "[youtube] Extracting URL", error }); };
    require (kindOf ("ERROR: Unsupported URL: https://example.com/") == FailureKind::unsupportedUrl, "unsupported");
    require (kindOf ("ERROR: [youtube] abc: Sign in to confirm you're not a bot. Use --cookies-from-browser or --cookies for the authentication.")
             == FailureKind::botCheck, "bot check (despite the cookies hint)");
    require (kindOf ("ERROR: [youtube] abc: Sign in to confirm your age. This video may be inappropriate for some users.")
             == FailureKind::needsAccount, "age-restricted");
    require (kindOf ("ERROR: [youtube] abc: Join this channel to get access to members-only content like this video")
             == FailureKind::needsAccount, "members-only");
    require (kindOf ("ERROR: [youtube] abc: Private video. Sign in if you've been granted access to this video")
             == FailureKind::needsAccount, "private");
    require (kindOf ("ERROR: [youtube] abc: The uploader has not made this video available in your country")
             == FailureKind::geoBlocked, "geo-blocked");
    require (kindOf ("ERROR: [youtube] abc: Video unavailable. This video has been removed by the uploader")
             == FailureKind::unavailable, "removed");
    require (kindOf ("ERROR: [generic] Unable to download webpage: <urlopen error [Errno -3] Temporary failure in name resolution>")
             == FailureKind::network, "offline");
    require (kindOf ("ERROR: [youtube] abc: Requested format is not available. Use --list-formats for a list of available formats")
             == FailureKind::noAudio, "no format");
    require (kindOf ("ERROR: [youtube] abc: nsig extraction failed: Some formats may be missing")
             == FailureKind::ytDlpOutdated, "site changed");
    require (kindOf ("ERROR: unable to download video data: HTTP Error 403: Forbidden") == FailureKind::ytDlpOutdated, "403");
    require (kindOf ("ERROR: Postprocessing: ffprobe and ffmpeg not found. Please install or provide the path using --ffmpeg-location")
             == FailureKind::other, "our ffmpeg missing is not a missing video");
    require (classifyFailure ({ "some output", "no error lines" }) == FailureKind::other, "no ERROR line");
    require (kindOf ("ERROR: Postprocessing: Unknown encoder 'flac'") == FailureKind::toolsOutdated, "no FLAC encoder");
    require (kindOf ("ERROR: Postprocessing: Error opening output files: Encoder not found") == FailureKind::toolsOutdated,
             "encoder not found");
    require (failureSuggestsUpdate (FailureKind::toolsOutdated), "old tools -> update");

    require (failureSuggestsUpdate (FailureKind::ytDlpOutdated), "outdated -> update");
    require (! failureSuggestsUpdate (FailureKind::needsAccount), "account -> no update");
    require (describeFailure (FailureKind::other, "boom").contains ("boom"), "raw error kept");
    require (describeFailure (FailureKind::botCheck, {}).contains ("Nightly"), "bot check suggests nightly");
}

void fileNamesAreSafe()
{
    requireEqual (makeOutputFileName ("Artist - Track", "id1"), "Artist - Track.wav", "plain title");
    require (! makeOutputFileName ("a/b\\c:d*e?f\"g<h>i|j", "id").containsAnyOf ("/\\:*?\"<>|"), "illegal characters removed");
    requireEqual (makeOutputFileName ("CON", "id"), "_CON.wav", "reserved Windows name");
    requireEqual (makeOutputFileName ("nul.txt", "id").substring (0, 1), "_", "reserved name with a dot");
    requireEqual (makeOutputFileName ("ends with dots...", "id"), "ends with dots.wav", "trailing dots dropped");
    requireEqual (makeOutputFileName ("   ", "abc123"), "abc123.wav", "empty title uses the id");
    requireEqual (makeOutputFileName ({}, {}), "Imported audio.wav", "nothing at all");
    requireEqual (makeOutputFileName ("Track", "id", "flac"), "Track.flac", "FLAC extension");
    requireEqual (makeOutputFileName ("Track", "id", ".flac"), "Track.flac", "extension with its dot");
    require (makeOutputFileName (juce::String::repeatedString ("x", 500), "id").length() <= 124, "long titles cut");
    const auto unicode = makeOutputFileName (juce::String (juce::CharPointer_UTF8 ("Bj\xc3\xb6rk \xe2\x80\x94 J\xc3\xb3ga")), "id");
    require (unicode.startsWith (juce::String (juce::CharPointer_UTF8 ("Bj\xc3\xb6rk"))), "non-ASCII kept");

    const auto folder = juce::File::createTempFile ("urlimport-names");
    require (folder.createDirectory().wasOk(), "temp folder");
    const juce::String name = "Song (Live).wav";
    requireEqual (uniqueFileIn (folder, name).getFileName(), name, "free name kept");
    folder.getChildFile (name).create();
    requireEqual (uniqueFileIn (folder, name).getFileName(), "Song (Live) (2).wav", "taken name numbered with a space");
    folder.getChildFile ("Song (Live) (2).wav").create();
    requireEqual (uniqueFileIn (folder, name).getFileName(), "Song (Live) (3).wav", "next number");
    folder.deleteRecursively();
}

//==============================================================================
#if ! JUCE_WINDOWS
juce::File shell() { return juce::File ("/bin/sh"); }

juce::StringArray readAll (ToolProcess& process)
{
    juce::StringArray lines;
    for (int i = 0; i < 400; ++i)
    {
        juce::String line;
        const auto read = process.readLine (line, 50);
        if (read == ToolProcess::ReadResult::line)
            lines.add (line);
        else if (read == ToolProcess::ReadResult::finished)
            return lines;
    }
    throw std::runtime_error ("output never finished");
}

void processLinesAndExitCode()
{
    ToolProcess process;
    juce::String error;
    require (process.start (shell(), { "-c", "printf 'one\\ntwo\\n'; printf 'no newline'; exit 3" }, error), "start: " + error.toStdString());
    const auto lines = readAll (process);
    require (lines.size() == 3 && lines[0] == "one" && lines[1] == "two" && lines[2] == "no newline", "lines in order");
    const auto code = process.waitForExit (2000);
    require (code.has_value() && *code == 3, "exit code");
}

void processArgumentsArriveVerbatim()
{
    ToolProcess process;
    juce::String error;
    const juce::String tricky = "a b \"c\" $HOME `x` ; rm -rf /";
    require (process.start (shell(), { "-c", "printf '%s\\n' \"$1\"", "sh", tricky }, error), "start");
    const auto lines = readAll (process);
    require (lines.size() == 1 && lines[0] == tricky, "no shell interpretation of the argument");
}

void processEnvironmentAndStdin()
{
    ::setenv ("PYTHONPATH", "/nowhere", 1);
    ToolProcess process;
    juce::String error;
    require (process.start (shell(), { "-c", "cat; echo \"[$PYTHONPATH][$PYTHONIOENCODING]\"" }, error), "start");
    const auto lines = readAll (process);   // cat sees EOF at once: stdin is /dev/null
    require (lines.size() == 1 && lines[0] == "[][utf-8]", "PYTHON* removed, UTF-8 output set");
    ::unsetenv ("PYTHONPATH");
}

void processTerminateKillsChildren()
{
    ToolProcess process;
    juce::String error;
    require (process.start (shell(), { "-c", "sleep 30 & echo $!; wait" }, error), "start");
    juce::String line;
    require (process.readLine (line, 5000) == ToolProcess::ReadResult::line, "child pid printed");
    const auto grandchild = (pid_t) line.getIntValue();
    require (grandchild > 0 && ::kill (grandchild, 0) == 0, "grandchild running");

    process.terminate();
    bool gone = false;
    for (int i = 0; i < 100 && ! gone; ++i)
    {
        gone = ::kill (grandchild, 0) != 0 && errno == ESRCH;
        if (! gone)
            juce::Thread::sleep (20);
    }
    require (gone, "terminate() also ended the program's own child");
    require (process.waitForExit (1000).has_value(), "program ended");
}

void processMissingExecutable()
{
    ToolProcess process;
    juce::String error;
    require (! process.start (juce::File ("/nonexistent/yt-dlp"), {}, error), "missing executable fails");
    require (error.isNotEmpty(), "with a reason");
}
#endif
} // namespace

int main()
{
    int failed = 0;
    int passed = 0;
    const auto run = [&] (const char* name, auto test)
    {
        try
        {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };

    run ("SHA2-256SUMS parsing", checksumsParse);
    run ("yt-dlp asset per platform and channel", assetsPerPlatform);
    run ("newest media release is picked", mediaReleaseIsPicked);
    run ("media manifest parsing and validation", manifestParses);
    run ("web links are recognised", webUrlsAreRecognised);
    run ("typed links are normalised", typedLinksAreNormalised);
    run ("yt-dlp arguments", ytDlpArgumentsAreSafe);
    run ("output formats", outputFormats);
    run ("yt-dlp output lines", outputLinesParse);
    run ("failures are classified", failuresAreClassified);
    run ("output file names are safe", fileNamesAreSafe);
   #if ! JUCE_WINDOWS
    run ("process: lines and exit code", processLinesAndExitCode);
    run ("process: arguments verbatim", processArgumentsArriveVerbatim);
    run ("process: environment and stdin", processEnvironmentAndStdin);
    run ("process: terminate kills children", processTerminateKillsChildren);
    run ("process: missing executable", processMissingExecutable);
   #endif

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
