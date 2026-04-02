#pragma once

#include <juce_core/juce_core.h>

namespace Intersect::LinuxDesktopSupport
{
inline bool shouldUseWaylandDndFallbackMessaging()
{
#if JUCE_LINUX
    if (juce::SystemStats::getEnvironmentVariable ("WAYLAND_DISPLAY", {}).trim().isNotEmpty())
        return true;

    return juce::SystemStats::getEnvironmentVariable ("XDG_SESSION_TYPE", {})
        .trim()
        .equalsIgnoreCase ("wayland");
#else
    return false;
#endif
}
}
