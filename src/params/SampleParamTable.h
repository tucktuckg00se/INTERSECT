#pragma once

#include <array>
#include "SampleParams.h"
#include "../audio/SampleData.h"

/** Sample-level parameters for every session sample, keyed by sampleId.

    Fixed size and allocation-free so the audio thread can own it and undo snapshots can copy
    it. Look-ups for unknown ids fall back to factory defaults.
*/
struct SampleParamTable
{
    struct Entry
    {
        int sampleId = -1;
        SampleParams params;
    };

    std::array<Entry, SampleData::kMaxSessionSamples> entries {};
    int count = 0;

    const SampleParams* find (int sampleId) const
    {
        for (int i = 0; i < count; ++i)
            if (entries[(size_t) i].sampleId == sampleId)
                return &entries[(size_t) i].params;
        return nullptr;
    }

    SampleParams* find (int sampleId)
    {
        return const_cast<SampleParams*> (static_cast<const SampleParamTable&> (*this).find (sampleId));
    }

    /** The sample's parameters, or factory defaults when it has no entry. */
    const SampleParams& get (int sampleId) const
    {
        static const SampleParams defaults = SampleParams::factoryDefaults();
        const auto* params = find (sampleId);
        return params != nullptr ? *params : defaults;
    }

    /** Sets (or adds) the sample's parameters. Returns false when the table is full. */
    bool set (int sampleId, const SampleParams& params)
    {
        if (auto* existing = find (sampleId))
        {
            *existing = params;
            return true;
        }
        if (count >= (int) entries.size())
            return false;
        entries[(size_t) count++] = { sampleId, params };
        return true;
    }

    /** Keeps only the given session samples; ids without an entry get factory defaults. */
    template <typename SessionSamples>
    void syncToSession (const SessionSamples& sessionSamples)
    {
        SampleParamTable synced;
        for (const auto& sample : sessionSamples)
            synced.set (sample.sampleId, get (sample.sampleId));
        *this = synced;
    }

    void clear() { count = 0; }
};
