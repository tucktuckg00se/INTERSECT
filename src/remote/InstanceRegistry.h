#pragma once

#include <juce_core/juce_core.h>

/** Discovery file for one running plugin instance, so remote controllers can find it.

    Each instance writes `<dir>/<instanceId>.json`:
        { "instanceId", "port", "pid", "protocolVersion", "pluginVersion", "trackName" }
    and deletes it on shutdown. Files left by crashed processes are pruned by pid on start.
    Message thread only.
*/
class InstanceRegistry
{
public:
    /** Linux: $XDG_STATE_HOME/intersect/instances (default ~/.local/state/...).
        Elsewhere: the per-user INTERSECT folder + /instances. */
    static juce::File getInstancesDir();

    InstanceRegistry() = default;
    ~InstanceRegistry() { remove(); }

    void publish (const juce::String& instanceId, int port, const juce::String& trackName);
    void remove();

private:
    static void pruneStaleFiles (const juce::File& dir);

    juce::File file;

    JUCE_DECLARE_NON_COPYABLE (InstanceRegistry)
};
