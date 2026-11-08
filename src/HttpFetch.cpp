#include "HttpFetch.h"

juce::String fetchText (const juce::URL& url, juce::Thread& thread, const juce::String& extraHeaders, int timeoutMs)
{
    if (thread.threadShouldExit())
        return {};

    int statusCode = 0;
    auto input = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (timeoutMs)
            .withNumRedirectsToFollow (5)
            .withStatusCode (&statusCode)
            .withExtraHeaders (extraHeaders));

    if (input == nullptr || statusCode != 200 || thread.threadShouldExit())
        return {};

    return input->readEntireStreamAsString();
}
