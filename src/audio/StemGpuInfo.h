#pragma once
#include <juce_core/juce_core.h>

// The GPU ONNX Runtime's DirectML provider uses by default (the high-performance hardware adapter),
// for the stem separation log and the copied error text. Windows only; invalid elsewhere.
struct StemGpuInfo
{
    bool valid = false;
    juce::String name;
    juce::String driverVersion;      // e.g. "32.0.15.8266"
    juce::int64 dedicatedMiB = 0;
    // How finely Windows can interrupt compute work on this GPU. "DMA_BUFFER" means only between
    // whole GPU jobs, so one long job trips the ~2 s timeout (TDR); newer GPUs report THREAD or finer.
    juce::String computePreemption;

    juce::String describe() const;
};

StemGpuInfo queryStemGpuInfo();
