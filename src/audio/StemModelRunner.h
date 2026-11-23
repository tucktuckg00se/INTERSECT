#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Runs a stem model on one float32 input tensor and returns its first output. The stem job drives ONNX
// Runtime (every OS) or a native Core ML model (the GPU on Macs) through this.
class StemModelRunner
{
public:
    virtual ~StemModelRunner() = default;

    // output and outputShape describe a contiguous float32 tensor that stays valid until the next call.
    // Throws std::exception on failure.
    virtual void run (const float* input, size_t inputCount, const std::vector<int64_t>& inputShape,
                      const float*& output, std::vector<int64_t>& outputShape) = 0;
};
