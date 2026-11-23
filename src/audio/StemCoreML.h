#pragma once

#include "StemModelRunner.h"
#include <juce_core/juce_core.h>
#include <memory>

// Loads a native Core ML stem model (.mlpackage) to run in fp32 on the CPU and GPU (never the Neural Engine,
// whose fp16 is too coarse for these models). The package is compiled once into cacheRoot and reused while
// it doesn't change. Returns null with error set when that isn't possible (not a Mac, macOS older than 13,
// a damaged package).
#if JUCE_MAC
std::unique_ptr<StemModelRunner> createCoreMlStemRunner (const juce::File& package, const juce::File& cacheRoot,
                                                         juce::String& error);
#else
inline std::unique_ptr<StemModelRunner> createCoreMlStemRunner (const juce::File&, const juce::File&, juce::String& error)
{
    error = "Core ML is only available on macOS";
    return nullptr;
}
#endif
