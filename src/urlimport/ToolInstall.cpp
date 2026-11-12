#include "ToolInstall.h"
#include "ToolProcess.h"
#include "../AppFiles.h"
#include "../Download.h"
#include "../audio/StemSeparation.h"   // getCurrentPlatformTag()

#if JUCE_MAC
 #include <sys/xattr.h>
#endif

namespace UrlImport
{
namespace
{
constexpr juce::int64 kCheckIntervalMs = 6 * 60 * 60 * 1000;
constexpr juce::int64 kStaleStagingMs = 24 * 60 * 60 * 1000;

juce::CriticalSection& processWideLock()
{
    static juce::CriticalSection lock;
    return lock;
}

juce::var newObject() { return juce::var (new juce::DynamicObject()); }

void setProperty (juce::var& object, const char* name, const juce::var& value)
{
    if (auto* dynamic = object.getDynamicObject())
        dynamic->setProperty (name, value);
}

/** A folder named in state.json, only if it stays inside root (the file could be edited by hand). */
juce::File childFolder (const juce::File& root, const juce::String& relative)
{
    if (relative.isEmpty() || juce::File::isAbsolutePath (relative) || relative.contains (".."))
        return {};
    return root.getChildFile (relative);
}

juce::String describeDownloadError (const juce::String& what, const juce::String& error)
{
    if (error == "cancelled")
        return error;
    if (error == "checksum mismatch")
        return "the " + what + " download was corrupted (checksum mismatch); try again";
    if (error.startsWith ("HTTP "))
        return "couldn't download " + what + " (" + error + ")";
    if (error == "network connection failed")
        return "couldn't download " + what + "; check your internet connection";
    return "couldn't save " + what + " (" + error + ")";
}

/** Deletes its paths when it goes out of scope, unless released. */
struct Cleanup
{
    std::vector<juce::File> paths;
    ~Cleanup()
    {
        for (const auto& path : paths)
            path.isDirectory() ? (void) path.deleteRecursively() : (void) path.deleteFile();
    }
};

struct RunResult
{
    bool started = false;
    int exitCode = -1;
    juce::StringArray lines;
    juce::String error;
};

/** Runs a tool to completion (a version check), collecting its output. */
RunResult runTool (const juce::File& executable, const juce::StringArray& args, int timeoutMs, const InstallContext& context)
{
    RunResult result;
    ToolProcess process;
    if (! process.start (executable, args, result.error))
        return result;
    result.started = true;

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        if (context.cancelled())
        {
            process.terminate();
            result.error = "cancelled";
            return result;
        }

        juce::String line;
        const auto read = process.readLine (line, 100);
        if (read == ToolProcess::ReadResult::line)
        {
            if (line.trim().isNotEmpty() && result.lines.size() < 200)
                result.lines.add (line.trim());
            continue;
        }
        if (read == ToolProcess::ReadResult::finished)
            break;
        if (juce::Time::getMillisecondCounter() > deadline)
        {
            process.terminate();
            result.error = "timed out";
            return result;
        }
    }

    auto code = process.waitForExit (5000);
    if (! code.has_value())
    {
        process.terminate();
        code = process.waitForExit (1000);
    }
    result.exitCode = code.value_or (-1);
    return result;
}

void makeExecutable (const juce::File& file)
{
   #if JUCE_WINDOWS
    juce::ignoreUnused (file);
   #else
    file.setExecutePermission (true);   // juce::ZipFile drops Unix modes
   #endif
}

/** Gatekeeper refuses quarantined downloads, including yt-dlp's bundled dylibs. INTERSECT's own
    downloads aren't quarantined, but a host may add the flag; clear it on everything. */
void clearQuarantine (const juce::File& folder)
{
   #if JUCE_MAC
    auto clear = [] (const juce::File& file)
    {
        ::removexattr (file.getFullPathName().toRawUTF8(), "com.apple.quarantine", XATTR_NOFOLLOW);
    };
    clear (folder);
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFilesAndDirectories))
        clear (entry.getFile());
   #else
    juce::ignoreUnused (folder);
   #endif
}

/** The onedir zip holds one top-level executable (yt-dlp_linux, yt-dlp_macos, yt-dlp.exe, ...) next to _internal/. */
juce::File findYtDlpExecutable (const juce::File& folder)
{
    juce::Array<juce::File> found;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "yt-dlp*"))
    {
       #if JUCE_WINDOWS
        if (file.hasFileExtension ("exe"))
       #else
        if (! file.hasFileExtension ("exe;zip"))
       #endif
            found.add (file);
    }
    return found.size() == 1 ? found.getFirst() : juce::File();
}

juce::String firstLine (const juce::StringArray& lines)
{
    return lines.isEmpty() ? juce::String() : lines[0];
}
} // namespace

//==============================================================================

/** Held while installing: one installer per process (two INTERSECT instances in a DAW share one)
    and one across processes. Waits with cancel checks instead of blocking. */
class ToolInstall::Lock
{
public:
    explicit Lock (const InstallContext& context) : interProcess ("INTERSECT-url-tools")
    {
        bool toldUser = false;
        auto waiting = [&]
        {
            if (! toldUser)
                context.progress ("Waiting for another INTERSECT to finish setting up URL import", -1.0f);
            toldUser = true;
        };

        while (! processWideLock().tryEnter())
        {
            if (context.cancelled())
                return;
            waiting();
            juce::Thread::sleep (100);
        }
        holdsProcessLock = true;

        while (! interProcess.enter (250))
        {
            if (context.cancelled())
                return;
            waiting();
        }
        holdsInterProcessLock = true;
    }

    ~Lock()
    {
        if (holdsInterProcessLock)
            interProcess.exit();
        if (holdsProcessLock)
            processWideLock().exit();
    }

    bool isHeld() const noexcept { return holdsProcessLock && holdsInterProcessLock; }

private:
    juce::InterProcessLock interProcess;
    bool holdsProcessLock = false;
    bool holdsInterProcessLock = false;
};

//==============================================================================

ToolInstall::ToolInstall (juce::File rootFolder) : root (std::move (rootFolder)) {}

juce::File ToolInstall::getDefaultRoot()
{
    return AppFiles::getDataDir().getChildFile ("url-import");
}

juce::String ToolInstall::getTestManifestUrl()
{
    return juce::SystemStats::getEnvironmentVariable (kTestManifestEnvVar, {}).trim();
}

juce::var ToolInstall::readState() const
{
    const auto file = root.getChildFile ("state.json");
    const auto parsed = file.existsAsFile() ? juce::JSON::parse (file) : juce::var();
    return parsed.getDynamicObject() != nullptr ? parsed : newObject();
}

bool ToolInstall::writeState (const juce::var& state) const
{
    // TemporaryFile + rename: another instance reading it never sees half a file.
    juce::TemporaryFile temp (root.getChildFile ("state.json"));
    return temp.getFile().replaceWithText (juce::JSON::toString (state)) && temp.overwriteTargetFileWithTemporary();
}

InstalledTools ToolInstall::readInstalled() const
{
    const auto state = readState();
    InstalledTools tools;

    const auto ytDlp = state["ytdlp"];
    if (const auto folder = childFolder (root, ytDlp["dir"].toString()); folder != juce::File())
    {
        const auto exe = ytDlp["exe"].toString();
        if (exe.isNotEmpty() && ! exe.containsAnyOf ("/\\"))
            tools.ytDlp = folder.getChildFile (exe);
        tools.ytDlpVersion = ytDlp["version"].toString();
        tools.ytDlpChannel = channelFromString (ytDlp["channel"].toString());
    }

    const auto media = state["media"];
    if (const auto folder = childFolder (root, media["dir"].toString()); folder != juce::File())
    {
        const auto ffmpeg = media["ffmpeg"].toString();
        const auto qjs = media["qjs"].toString();
        if (ffmpeg.isNotEmpty() && ! ffmpeg.containsAnyOf ("/\\"))
            tools.ffmpeg = folder.getChildFile (ffmpeg);
        if (qjs.isNotEmpty() && ! qjs.containsAnyOf ("/\\"))
            tools.qjs = folder.getChildFile (qjs);
        tools.mediaRelease = media["release"].toString();
        tools.ffmpegVersion = media["ffmpegVersion"].toString();
        tools.qjsVersion = media["qjsVersion"].toString();
    }
    return tools;
}

ToolInstall::Outcome ToolInstall::ensure (const InstallContext& context, Channel channel, bool forceCheck)
{
    Outcome out;
    const Lock lock (context);
    if (! lock.isHeld())
    {
        out.cancelled = true;
        return out;
    }

    if (const auto created = root.createDirectory(); created.failed())
    {
        out.error = "couldn't create " + root.getFullPathName() + " (" + created.getErrorMessage() + ")";
        return out;
    }

    auto state = readState();
    collectGarbage (state);
    out.tools = readInstalled();

    const auto platformTag = getCurrentPlatformTag();
    const auto asset = ytDlpAssetName (platformTag);
    if (asset.isEmpty())
    {
        out.error = "URL import isn't available on this platform";
        return out;
    }

    const auto now = juce::Time::currentTimeMillis();
    const auto lastCheck = (juce::int64) state["lastCheckMs"];
    const bool channelChanged = out.tools.hasYtDlp() && out.tools.ytDlpChannel != channel;
    const bool due = forceCheck || ! out.tools.isComplete() || channelChanged
                  || now - lastCheck > kCheckIntervalMs || now < lastCheck;
    if (! due)
        return out;

    bool checkFailed = false;
    auto keepInstalled = [&out, &checkFailed] (const juce::String& note)
    {
        checkFailed = true;
        if (out.warning.isEmpty())
            out.warning = note;
    };

    // yt-dlp: the release's SHA2-256SUMS says whether ours is still the latest.
    context.progress ("Checking for a newer yt-dlp", -1.0f);
    const auto sums = Download::fetchTextOrFile (juce::URL (ytDlpDownloadBaseUrl (channel) + "SHA2-256SUMS"), context.thread);
    if (context.cancelled())
    {
        out.cancelled = true;
        return out;
    }

    const auto latestSha = findSha256 (sums, asset);
    if (latestSha.isEmpty())
    {
        if (! out.tools.hasYtDlp())
        {
            out.error = sums.isEmpty() ? "couldn't reach GitHub to download yt-dlp; check your internet connection"
                                       : "yt-dlp's latest release has no " + asset;
            return out;
        }
        keepInstalled ("Couldn't check for a newer yt-dlp, so the installed one was used.");
    }
    else if (latestSha != state["ytdlp"]["sha256"].toString() || ! out.tools.hasYtDlp() || channelChanged)
    {
        const auto error = installYtDlp (context, state, asset, latestSha, channel);
        if (context.cancelled())
        {
            out.cancelled = true;
            return out;
        }
        if (error.isNotEmpty())
        {
            if (! out.tools.hasYtDlp())
            {
                out.error = error;
                return out;
            }
            keepInstalled ("Couldn't update yt-dlp (" + error + "), so the installed one was used.");
        }
        else
        {
            out.ytDlpChanged = true;
        }
    }

    // ffmpeg + qjs: the newest media release's manifest (or the test hook's).
    context.progress ("Checking the audio tools", -1.0f);
    juce::String mediaError;
    MediaToolsEntry entry;
    const auto manifestUrl = resolveMediaManifestUrl (context);
    if (context.cancelled())
    {
        out.cancelled = true;
        return out;
    }
    if (manifestUrl.isEmpty())
    {
        mediaError = "couldn't find INTERSECT's audio tools on GitHub; check your internet connection";
    }
    else
    {
        const auto manifest = Download::fetchTextOrFile (juce::URL (manifestUrl), context.thread);
        if (manifest.isEmpty())
            mediaError = "couldn't download the audio tools list (" + manifestUrl + ")";
        else
            entry = parseMediaToolsManifest (manifest, platformTag, mediaError);
    }

    if (mediaError.isNotEmpty())
    {
        if (! out.tools.hasMediaTools())
        {
            out.error = mediaError;
            return out;
        }
        keepInstalled ("Couldn't check for newer audio tools, so the installed ones were used.");
    }
    else if (entry.sha256 != state["media"]["sha256"].toString() || ! out.tools.hasMediaTools())
    {
        const auto error = installMediaTools (context, state, entry);
        if (context.cancelled())
        {
            out.cancelled = true;
            return out;
        }
        if (error.isNotEmpty())
        {
            if (! out.tools.hasMediaTools())
            {
                out.error = error;
                return out;
            }
            keepInstalled ("Couldn't update the audio tools (" + error + "), so the installed ones were used.");
        }
        else
        {
            out.mediaChanged = true;
        }
    }

    if (! checkFailed)
    {
        setProperty (state, "lastCheckMs", now);   // a failed check is retried on the next import
        writeState (state);
    }
    collectGarbage (state);

    out.tools = readInstalled();
    if (! out.tools.isComplete() && out.error.isEmpty())
        out.error = "the URL import tools are incomplete; try UPDATE NOW in Settings > URL Import";
    return out;
}

juce::String ToolInstall::installYtDlp (const InstallContext& context, juce::var& state, const juce::String& asset,
                                        const juce::String& sha256, Channel channel)
{
    const auto staging = getStagingDir();
    staging.createDirectory();
    const auto id = juce::Uuid().toString();
    const auto zip = staging.getChildFile ("ytdlp-" + id + ".zip");
    const auto unpacked = staging.getChildFile ("ytdlp-" + id);
    Cleanup cleanup { { zip, unpacked } };

    juce::String error;
    const juce::String stage = channel == Channel::nightly ? "Downloading yt-dlp (nightly)" : "Downloading yt-dlp";
    context.progress (stage, 0.0f);
    if (! Download::downloadFileToTarget (juce::URL (ytDlpDownloadBaseUrl (channel) + asset), zip, context.cancel,
                                          context.thread, error, sha256,
                                          [&context, &stage] (juce::int64 received, juce::int64 total)
                                          {
                                              context.progress (stage, total > 0 ? (float) ((double) received / (double) total) : -1.0f);
                                          }))
        return describeDownloadError ("yt-dlp", error);

    context.progress ("Installing yt-dlp", -1.0f);
    if (! Download::extractZipToFolder (zip, unpacked, error))
        return "couldn't unpack yt-dlp (" + error + ")";
    zip.deleteFile();

    const auto exe = findYtDlpExecutable (unpacked);
    if (exe == juce::File())
        return "the yt-dlp download has an unexpected layout";
    makeExecutable (exe);
    clearQuarantine (unpacked);

    // A real run proves it starts here (antivirus, missing system libraries) before it is used.
    const auto run = runTool (exe, { "--version" }, 120000, context);
    if (context.cancelled())
        return "cancelled";
    if (! run.started)
        return "yt-dlp couldn't start (" + run.error + ")";
    const auto version = firstLine (run.lines);
    if ((run.exitCode != 0 && run.exitCode != -1) || version.isEmpty() || ! version.containsOnly ("0123456789."))
        return "yt-dlp didn't run (" + (version.isNotEmpty() ? version : run.error.isNotEmpty() ? run.error : "exit code " + juce::String (run.exitCode)) + ")";

    const auto relative = "ytdlp/" + version + "-" + sha256.substring (0, 12);
    const auto destination = root.getChildFile (relative);
    destination.getParentDirectory().createDirectory();
    destination.deleteRecursively();
    if (! unpacked.moveFileTo (destination))
        return "couldn't move yt-dlp into " + destination.getFullPathName();

    auto entry = newObject();
    setProperty (entry, "dir", relative);
    setProperty (entry, "exe", exe.getFileName());
    setProperty (entry, "sha256", sha256);
    setProperty (entry, "asset", asset);
    setProperty (entry, "version", version);
    setProperty (entry, "channel", channelToString (channel));
    const auto previous = state["ytdlp"]["dir"].toString();
    if (previous.isNotEmpty() && previous != relative)
        setProperty (state, "ytdlpPrevious", previous);
    setProperty (state, "ytdlp", entry);
    writeState (state);
    return {};
}

juce::String ToolInstall::installMediaTools (const InstallContext& context, juce::var& state, const MediaToolsEntry& entry)
{
    const auto staging = getStagingDir();
    staging.createDirectory();
    const auto id = juce::Uuid().toString();
    const auto zip = staging.getChildFile ("media-" + id + ".zip");
    const auto unpacked = staging.getChildFile ("media-" + id);
    Cleanup cleanup { { zip, unpacked } };

    juce::String error;
    const juce::String stage = "Downloading audio tools";
    context.progress (stage, 0.0f);
    if (! Download::downloadFileToTarget (juce::URL (entry.downloadUrl), zip, context.cancel, context.thread, error,
                                          entry.sha256,
                                          [&context, &stage, expected = entry.bytes] (juce::int64 received, juce::int64 total)
                                          {
                                              if (expected > 0)
                                                  total = expected;
                                              context.progress (stage, total > 0 ? (float) ((double) received / (double) total) : -1.0f);
                                          }))
        return describeDownloadError ("the audio tools", error);

    context.progress ("Installing audio tools", -1.0f);
    if (! Download::extractZipToFolder (zip, unpacked, error))
        return "couldn't unpack the audio tools (" + error + ")";
    zip.deleteFile();

    const auto ffmpeg = unpacked.getChildFile (entry.ffmpegFile);
    const auto qjs = unpacked.getChildFile (entry.qjsFile);
    if (! ffmpeg.existsAsFile() || ! qjs.existsAsFile())
        return "the audio tools download is missing " + (! ffmpeg.existsAsFile() ? entry.ffmpegFile : entry.qjsFile);
    makeExecutable (ffmpeg);
    makeExecutable (qjs);
    clearQuarantine (unpacked);

    const auto ffmpegRun = runTool (ffmpeg, { "-hide_banner", "-version" }, 30000, context);
    if (context.cancelled())
        return "cancelled";
    if (! ffmpegRun.started || (ffmpegRun.exitCode != 0 && ffmpegRun.exitCode != -1))
        return "ffmpeg couldn't run (" + (ffmpegRun.error.isNotEmpty() ? ffmpegRun.error : firstLine (ffmpegRun.lines)) + ")";
    const auto qjsRun = runTool (qjs, { "-e", "print(6 * 7)" }, 30000, context);
    if (context.cancelled())
        return "cancelled";
    if (! qjsRun.started || firstLine (qjsRun.lines) != "42")
        return "QuickJS couldn't run (" + (qjsRun.error.isNotEmpty() ? qjsRun.error : firstLine (qjsRun.lines)) + ")";

    const auto name = juce::File::createLegalFileName (entry.release.isNotEmpty() ? entry.release : juce::String ("media"));
    const auto relative = "media/" + name + "-" + entry.sha256.substring (0, 12);
    const auto destination = root.getChildFile (relative);
    destination.getParentDirectory().createDirectory();
    destination.deleteRecursively();
    if (! unpacked.moveFileTo (destination))
        return "couldn't move the audio tools into " + destination.getFullPathName();

    auto media = newObject();
    setProperty (media, "dir", relative);
    setProperty (media, "sha256", entry.sha256);
    setProperty (media, "release", entry.release);
    setProperty (media, "ffmpeg", entry.ffmpegFile);
    setProperty (media, "qjs", entry.qjsFile);
    setProperty (media, "ffmpegVersion", entry.ffmpegVersion);
    setProperty (media, "qjsVersion", entry.qjsVersion);
    const auto previous = state["media"]["dir"].toString();
    if (previous.isNotEmpty() && previous != relative)
        setProperty (state, "mediaPrevious", previous);
    setProperty (state, "media", media);
    writeState (state);
    return {};
}

juce::String ToolInstall::resolveMediaManifestUrl (const InstallContext& context) const
{
    if (const auto testUrl = getTestManifestUrl(); testUrl.isNotEmpty())
        return testUrl;

    const auto releases = Download::fetchTextOrFile (juce::URL (kToolsReleasesApiUrl), context.thread);
    return releases.isNotEmpty() ? pickMediaManifestUrl (juce::JSON::parse (releases)) : juce::String();
}

void ToolInstall::collectGarbage (const juce::var& state) const
{
    // Leftovers from imports or installs that crashed. Fresh entries may belong to a running job.
    const auto now = juce::Time::getCurrentTime();
    for (const auto& entry : juce::RangedDirectoryIterator (getStagingDir(), false, "*", juce::File::findFilesAndDirectories))
        if ((now - entry.getModificationTime()).inMilliseconds() > kStaleStagingMs)
            entry.getFile().isDirectory() ? (void) entry.getFile().deleteRecursively() : (void) entry.getFile().deleteFile();

    // Keep the current and the previous version of each tool: another INTERSECT may still be
    // running the previous one. On Windows a folder that is in use simply fails to delete.
    auto prune = [this, &state] (const char* folder, const char* current, const char* previous)
    {
        const auto keepA = childFolder (root, state[current]["dir"].toString());
        const auto keepB = childFolder (root, state[previous].toString());
        for (const auto& entry : juce::RangedDirectoryIterator (root.getChildFile (folder), false, "*", juce::File::findDirectories))
            if (entry.getFile() != keepA && entry.getFile() != keepB)
                entry.getFile().deleteRecursively();
    };
    prune ("ytdlp", "ytdlp", "ytdlpPrevious");
    prune ("media", "media", "mediaPrevious");
}

bool ToolInstall::removeAll (const InstallContext& context, juce::String& error)
{
    const Lock lock (context);
    if (! lock.isHeld())
    {
        error = "cancelled";
        return false;
    }

    context.progress ("Removing the URL import tools", -1.0f);
    if (root.exists() && ! root.deleteRecursively())
    {
        error = "some files are in use (is another INTERSECT importing?); close it and try again";
        return false;
    }
    return true;
}
} // namespace UrlImport
