#include "InstanceRegistry.h"
#include "RemoteProtocol.h"
#include "../AppFiles.h"

#if JUCE_LINUX || JUCE_MAC
 #include <cerrno>
 #include <csignal>
 #include <unistd.h>
#elif JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
int currentPid()
{
#if JUCE_WINDOWS
    return (int) GetCurrentProcessId();
#else
    return (int) getpid();
#endif
}

bool processIsAlive (int pid)
{
    if (pid <= 0)
        return false;
#if JUCE_WINDOWS
    if (auto handle = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid))
    {
        DWORD code = 0;
        const bool alive = GetExitCodeProcess (handle, &code) && code == STILL_ACTIVE;
        CloseHandle (handle);
        return alive;
    }
    return false;
#else
    return kill ((pid_t) pid, 0) == 0 || errno == EPERM;
#endif
}
} // namespace

juce::File InstanceRegistry::getInstancesDir()
{
#if JUCE_LINUX
    const auto xdgState = juce::SystemStats::getEnvironmentVariable ("XDG_STATE_HOME", {});
    const auto stateRoot = xdgState.isNotEmpty()
        ? juce::File (xdgState)
        : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/state");
    return stateRoot.getChildFile ("intersect").getChildFile ("instances");
#else
    return AppFiles::getSettingsDir().getChildFile ("instances");
#endif
}

void InstanceRegistry::publish (const juce::String& instanceId, int port, const juce::String& trackName)
{
    const auto dir = getInstancesDir();
    if (! dir.createDirectory())
        return;

    if (file == juce::File())
        pruneStaleFiles (dir);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("instanceId", instanceId);
    obj->setProperty ("port", port);
    obj->setProperty ("pid", currentPid());
    obj->setProperty ("protocolVersion", RemoteProtocol::kVersion);
    obj->setProperty ("pluginVersion", juce::String (JucePlugin_VersionString));
    obj->setProperty ("trackName", trackName);

    const auto target = dir.getChildFile (instanceId + ".json");
    const auto temp = target.getSiblingFile (target.getFileName() + ".tmp");
    if (temp.replaceWithText (juce::JSON::toString (juce::var (obj))) && temp.moveFileTo (target))
        file = target;
}

void InstanceRegistry::remove()
{
    if (file != juce::File())
        file.deleteFile();
    file = juce::File();
}

void InstanceRegistry::pruneStaleFiles (const juce::File& dir)
{
    for (const auto& entry : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        const auto parsed = juce::JSON::parse (entry);
        if (! processIsAlive ((int) parsed.getProperty ("pid", 0)))
            entry.deleteFile();
    }
}
