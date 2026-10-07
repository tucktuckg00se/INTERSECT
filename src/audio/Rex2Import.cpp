#include "Rex2Import.h"
#include <velociloops.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace Rex2Import
{
namespace
{
// REX positions use 15360 ticks per QUARTER note. velociloops.h describes kREXPPQ as "per bar",
// but its own original_tempo computation treats ppq_length / 15360 as beats, and the test loops
// only fit their audio length that way (e.g. 120Stereo.rx2: last slice at beat 3.5 of ~4.2).
constexpr double kRexPpqPerQuarter = 15360.0;

struct FileCloser
{
    void operator() (VLFile_s* f) const { vl_close (f); }
};
using FileHandle = std::unique_ptr<VLFile_s, FileCloser>;

// Read through JUCE rather than vl_open(): vl_open() takes a narrow path, which
// breaks on Windows for paths outside the system code page.
FileHandle openFile (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() == 0)
        return {};

    VLError err = VL_OK;
    return FileHandle (vl_open_from_memory (data.getData(), data.getSize(), &err));
}
} // namespace

bool decodeFile (const juce::File& file, double targetSampleRate, DecodedLoop& out)
{
    auto f = openFile (file);
    if (f == nullptr)
        return false;

    VLFileInfo info {};
    if (vl_get_info (f.get(), &info) != VL_OK
        || info.slice_count <= 0
        || info.sample_rate <= 0)
        return false;

    // Render every slice (with its transient-stretch tail, if any) and concatenate
    // the results into one continuous stereo buffer. The REX2 metadata says where
    // each slice lives in the original loop, but because rendered slices can be
    // longer than their raw length we track the actual offsets as we go.
    std::vector<float> left, right;
    std::vector<SliceSpan> spans;
    spans.reserve ((size_t) info.slice_count);

    for (int32_t i = 0; i < info.slice_count; ++i)
    {
        VLSliceInfo sliceInfo {};
        const int32_t n = vl_get_slice_frame_count (f.get(), i);
        if (n <= 0 || vl_get_slice_info (f.get(), i, &sliceInfo) != VL_OK)
            return false;

        std::vector<float> L ((size_t) n), R ((size_t) n);
        int32_t written = 0;
        if (vl_decode_slice (f.get(), i, L.data(), R.data(), 0, n, &written) != VL_OK || written <= 0)
            return false;

        const int start = (int) left.size();
        left.insert  (left.end(),  L.begin(), L.begin() + written);
        right.insert (right.end(), R.begin(), R.begin() + written);
        spans.push_back ({ start, start + (int) written, (double) sliceInfo.ppq_pos / kRexPpqPerQuarter });
    }

    if (left.empty())
        return false;

    const double srcRate = (double) info.sample_rate;
    const double target  = targetSampleRate > 0.0 ? targetSampleRate : srcRate;

    DecodedLoop loop;
    loop.sourceSampleRate = srcRate;
    loop.sourceNumFrames = (int) left.size();
    // original_tempo is derived by VelociLoops from the audio's actual length and beat count, so it
    // is the tempo the rendered audio really plays at; `tempo` is only the playback tempo set in
    // ReCycle and can differ when the loop was re-tempoed there.
    const int32_t milliBpm = info.original_tempo > 0 ? info.original_tempo : info.tempo;
    loop.tempoBpm = (float) ((double) milliBpm / 1000.0);
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
    std::copy (left.begin(),  left.end(),  loop.stereo.getWritePointer (0));
    std::copy (right.begin(), right.end(), loop.stereo.getWritePointer (1));
    loop.slices = std::move (spans);

    out = std::move (loop);
    return true;
}

bool readInfo (const juce::File& file, FileInfo& out)
{
    auto f = openFile (file);
    if (f == nullptr)
        return false;

    VLFileInfo info {};
    if (vl_get_info (f.get(), &info) != VL_OK || info.sample_rate <= 0)
        return false;

    out.sampleRate = (double) info.sample_rate;
    out.numChannels = (int) info.channels;
    out.bitsPerSample = (int) info.bit_depth;
    out.lengthSeconds = (double) juce::jmax (0, (int) info.total_frames) / (double) info.sample_rate;
    return true;
}

} // namespace Rex2Import
