#include "Rex2Import.h"
#include <velociloops.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Rex2Import
{

bool decodeFile (const juce::File& file, double targetSampleRate, DecodedLoop& out)
{
    VLError err = VL_OK;
    VLFile f = vl_open (file.getFullPathName().toRawUTF8(), &err);
    if (f == nullptr)
        return false;

    VLFileInfo info {};
    if (vl_get_info (f, &info) != VL_OK
        || info.slice_count <= 0
        || info.sample_rate <= 0)
    {
        vl_close (f);
        return false;
    }

    // Render every slice (with its transient-stretch tail, if any) and concatenate
    // the results into one continuous stereo buffer. The REX2 metadata says where
    // each slice lives in the original loop, but because rendered slices can be
    // longer than their raw length we track the actual offsets as we go.
    std::vector<float> left, right;
    std::vector<SliceSpan> spans;
    spans.reserve ((size_t) info.slice_count);

    for (int32_t i = 0; i < info.slice_count; ++i)
    {
        const int32_t n = vl_get_slice_frame_count (f, i);
        if (n <= 0)
        {
            vl_close (f);
            return false;
        }

        std::vector<float> L ((size_t) n), R ((size_t) n);
        int32_t written = 0;
        if (vl_decode_slice (f, i, L.data(), R.data(), 0, n, &written) != VL_OK || written <= 0)
        {
            vl_close (f);
            return false;
        }

        const int start = (int) left.size();
        left.insert  (left.end(),  L.begin(), L.begin() + written);
        right.insert (right.end(), R.begin(), R.begin() + written);
        spans.push_back ({ start, start + (int) written });
    }

    vl_close (f);

    if (left.empty())
        return false;

    const double srcRate = (double) info.sample_rate;
    const double target  = targetSampleRate > 0.0 ? targetSampleRate : srcRate;

    DecodedLoop loop;
    loop.tempoBpm = (float) ((double) info.tempo / 1000.0);
    if (! std::isfinite (loop.tempoBpm) || loop.tempoBpm <= 0.0f)
        loop.tempoBpm = 120.0f;

    int totalFrames = (int) left.size();

    if (std::abs (srcRate - target) > 0.01)
    {
        const double ratio = srcRate / target;
        const int resampledLen = (int) std::ceil ((double) totalFrames / ratio);
        if (resampledLen <= 0)
            return false;

        std::vector<float> rL ((size_t) resampledLen), rR ((size_t) resampledLen);
        juce::LagrangeInterpolator interpL, interpR;
        interpL.process (ratio, left.data(),  rL.data(), resampledLen);
        interpR.process (ratio, right.data(), rR.data(), resampledLen);
        left  = std::move (rL);
        right = std::move (rR);
        totalFrames = resampledLen;

        // Resampling is a linear time mapping, so scale the slice spans by the same ratio.
        const double scale = target / srcRate;
        for (auto& s : spans)
        {
            s.startSample = juce::jlimit (0, totalFrames,
                                          (int) std::lround ((double) s.startSample * scale));
            s.endSample   = juce::jlimit (s.startSample, totalFrames,
                                          (int) std::lround ((double) s.endSample * scale));
        }
    }

    loop.sampleRate = target;
    loop.stereo.setSize (2, totalFrames);
    if (totalFrames > 0)
    {
        std::copy (left.begin(),  left.end(),  loop.stereo.getWritePointer (0));
        std::copy (right.begin(), right.end(), loop.stereo.getWritePointer (1));
    }
    loop.slices = std::move (spans);

    out = std::move (loop);
    return true;
}

} // namespace Rex2Import
