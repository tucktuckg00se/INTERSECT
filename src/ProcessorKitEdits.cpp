// Removing session samples from the kit (one, or a whole group as one step). The message thread
// builds the shortened kit; the audio thread swaps it in (SampleRemoval, deleteSessionSample).

#include "PluginProcessor.h"

void IntersectProcessor::deleteSessionSampleAsync (int sampleId)
{
    if (sampleId >= 0)
        deleteSessionSamplesAsync ({ sampleId });
}

void IntersectProcessor::deleteSessionSamplesAsync (const std::vector<int>& sampleIds)
{
    // Build the shortened kit here (it copies the remaining audio), so the audio thread only swaps it in.
    const auto source = sampleData.getSnapshot();
    if (source == nullptr || source->sessionSamples.empty())
        return;

    const auto& samples = source->sessionSamples;
    SampleRemoval removal;
    for (int id : sampleIds)
    {
        const bool inKit = std::any_of (samples.begin(), samples.end(), [id] (const auto& s) { return s.sampleId == id; });
        const auto end = removal.sampleIds.begin() + removal.numSampleIds;
        if (inKit && std::find (removal.sampleIds.begin(), end, id) == end && removal.numSampleIds < (int) removal.sampleIds.size())
            removal.sampleIds[(size_t) removal.numSampleIds++] = id;
    }
    if (removal.numSampleIds == 0)
        return;

    const auto removing = [&removal] (int id)
    {
        return std::find (removal.sampleIds.begin(), removal.sampleIds.begin() + removal.numSampleIds, id)
               != removal.sampleIds.begin() + removal.numSampleIds;
    };

    removal.sampleId = removal.sampleIds[0];
    removal.source = source;
    std::vector<SampleData::SessionSample> remaining;
    size_t firstRemovedIndex = samples.size();
    for (size_t i = 0; i < samples.size(); ++i)
    {
        if (! removing (samples[i].sampleId))
            remaining.push_back (samples[i]);
        else if (firstRemovedIndex == samples.size())
            firstRemovedIndex = remaining.size();
    }
    if (! remaining.empty())
    {
        removal.nextSelectedSampleId = remaining[juce::jmin (firstRemovedIndex, remaining.size() - 1)].sampleId;
        removal.rebuilt = SampleData::rebuildWithSessionSamples (*source, remaining);
    }

    const int firstId = removal.sampleId;
    if (! sampleRemovals.push (std::move (removal)))
        return;   // sixteen removals already waiting for the audio thread

    Command cmd;
    cmd.type = CmdDeleteSessionSample;
    cmd.intParam1 = firstId;
    pushCommand (cmd);
}

void IntersectProcessor::deleteSessionSample (int sampleId)
{
    // Take the removal prepared for this sample (older ones whose command never ran are dropped).
    SampleRemoval removal;
    bool found = false;
    while (! found && sampleRemovals.pop (removal))
    {
        found = removal.sampleId == sampleId;
        if (! found)
        {
            RetiredObjects stale;
            stale.sample = std::move (removal.source);
            retire (std::move (stale));
            stale.sample = std::move (removal.rebuilt);
            retire (std::move (stale));
        }
    }

    // Only apply it to the kit it was built from; a load that landed in between wins.
    if (found && removal.source.get() == sampleData.getActive())
    {
        if (removal.rebuilt == nullptr)
        {
            clearVoicesBeforeSampleSwap();
            RetiredObjects unloaded;
            unloaded.sample = sampleData.clear();
            retire (std::move (unloaded));
            sampleParams.clear();
            sampleGroups = {};
            sliceManager.clearAll();
            updateSampleMissingState();
            selectedSessionSampleId.store (-1, std::memory_order_relaxed);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
        else
        {
            // A group anchor hands its slices to the next member first, so they survive.
            for (int k = 0; k < removal.numSampleIds; ++k)
                prepareGroupsForSampleRemoval (removal.sampleIds[(size_t) k]);
            const auto removed = [&removal] (int id)
            {
                for (int k = 0; k < removal.numSampleIds; ++k)
                    if (removal.sampleIds[(size_t) k] == id)
                        return true;
                return false;
            };

            const int oldSelectedSlice = sliceManager.selectedSlice.load (std::memory_order_relaxed);
            const int oldNumSlices = sliceManager.getNumSlices();
            int writeSlice = 0;
            int nextSelectedSlice = -1;
            for (int i = 0; i < oldNumSlices; ++i)
            {
                const auto& src = sliceManager.getSlice (i);
                if (removed (src.sampleId))
                    continue;

                sliceManager.getSlice (writeSlice) = src;
                if (i == oldSelectedSlice)
                    nextSelectedSlice = writeSlice;
                ++writeSlice;
            }

            for (int i = writeSlice; i < SliceManager::kMaxSlices; ++i)
                sliceManager.getSlice (i).active = false;
            sliceManager.setNumSlices (writeSlice);
            sliceManager.selectedSlice.store (nextSelectedSlice, std::memory_order_relaxed);

            clearVoicesBeforeSampleSwap();
            RetiredObjects replaced;
            replaced.sample = sampleData.applyDecodedSample (std::move (removal.rebuilt));
            retire (std::move (replaced));
            sampleParams.syncToSession (sampleData.getSessionSamples());
            pruneGroupsToSession();
            updateSampleMissingState();
            selectedSessionSampleId.store (removal.nextSelectedSampleId, std::memory_order_relaxed);
            syncAllSliceAbsolutePositions();
            clampSlicesToSampleBounds();
            deselectParkedSlice();
            sliceManager.rebuildMidiMap();
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    // Whatever the removal still holds is freed on the message thread.
    RetiredObjects done;
    done.sample = std::move (removal.source);
    retire (std::move (done));
    done.sample = std::move (removal.rebuilt);
    retire (std::move (done));
}

void IntersectProcessor::duplicateSessionSampleAsync (int sampleId)
{
    const auto snapshot = sampleData.getSnapshot();
    if (snapshot == nullptr)
        return;

    const SampleData::SessionSample* source = nullptr;
    for (const auto& sample : snapshot->sessionSamples)
        if (sample.sampleId == sampleId)
            source = &sample;
    if (source == nullptr)
        return;

    const auto file = juce::File::createFileWithoutCheckingPath (source->filePath);
    if (source->isPlaceholder() || ! file.existsAsFile())
    {
        setUiStatusMessage ("This sample's file is missing - relink it first", true);
        return;
    }

    if (pendingStemImport.load (std::memory_order_acquire) != nullptr)
    {
        setUiStatusMessage ("Wait for the samples being added to finish loading", true);
        return;
    }

    // The copy's settings and slices are applied when the file lands (applyPendingKitAddition).
    auto* pending = new PendingStemImport();
    pending->duplicateOf = sampleId;
    delete pendingStemImport.exchange (pending, std::memory_order_acq_rel);
    stopAudition();
    enqueueUiUndoSnapshot();
    loadFilesAsync ({ file }, true, false);   // a REX2 copy takes the source's slices, not the file's
}

void IntersectProcessor::addStemsToKit (int sourceSampleId, const std::vector<juce::File>& files,
                                        const std::vector<juce::String>& names, bool asGroup)
{
    if (files.empty())
        return;

    // Applied when the files land (applyCompletedLoad, then applyPendingKitAddition).
    auto* pending = new PendingStemImport();
    pending->parentSourceSampleId = sourceSampleId;
    pending->groupStems = asGroup && files.size() >= 2;
    for (const auto& name : names)
    {
        StemMetadata meta;
        meta.parentSourceSampleId = sourceSampleId;
        meta.role = stemRoleFromString (name);
        meta.isGenerated = true;
        meta.setName (name);
        pending->metas.push_back (meta);
    }
    delete pendingStemImport.exchange (pending, std::memory_order_acq_rel);
    loadFilesAsync (files, true);
}
