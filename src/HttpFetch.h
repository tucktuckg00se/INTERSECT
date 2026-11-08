#pragma once
#include <juce_core/juce_core.h>

/** Small text fetch (a manifest, a GitHub API response) with a short timeout, so a worker thread
    can stop quickly when the plugin is unloaded. Empty on any failure or non-200 status.
    Blocking: worker threads only. */
juce::String fetchText (const juce::URL& url, juce::Thread& thread,
                        const juce::String& extraHeaders = {}, int timeoutMs = 10000);
