// IntersectProcessor's command handling: the UI-to-audio command FIFO (push, overflow,
// coalesced drags), draining it at the start of each block, undo/redo snapshots and
// handleCommand. Everything below drainCommands runs on the audio thread (G1 in CLAUDE.md).

#include "PluginProcessor.h"
#include "ProcessorInternal.h"
#include "Constants.h"
#include "audio/AudioAnalysis.h"
#include "audio/GrainEngine.h"
#include "audio/SliceEdits.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using ProcessorInternal::kMidiEditMinSliceLength;
using ProcessorInternal::makePreviewStretchParams;

namespace
{
static bool isCoalescableCommand (IntersectProcessor::CommandType type)
{
    return type == IntersectProcessor::CmdSetSliceParam
        || type == IntersectProcessor::CmdSetSampleParam
        || type == IntersectProcessor::CmdSetSliceBounds;
}

static bool isCriticalCommand (IntersectProcessor::CommandType type)
{
    switch (type)
    {
        case IntersectProcessor::CmdNone:
        case IntersectProcessor::CmdLazyChopStart:
        case IntersectProcessor::CmdLazyChopStop:
        case IntersectProcessor::CmdStretch:
        case IntersectProcessor::CmdToggleLock:
        case IntersectProcessor::CmdSetSliceParam:
        case IntersectProcessor::CmdSetSampleParam:
        case IntersectProcessor::CmdSetSliceBounds:
        case IntersectProcessor::CmdRepackMidi:
        case IntersectProcessor::CmdFileLoadCompleted:
        case IntersectProcessor::CmdFileLoadFailed:
        case IntersectProcessor::CmdBeginGesture:
            return false;

        case IntersectProcessor::CmdCreateSlice:
        case IntersectProcessor::CmdDeleteSlice:
        case IntersectProcessor::CmdDeleteSessionSample:
        case IntersectProcessor::CmdDuplicateSlice:
        case IntersectProcessor::CmdSplitSlice:
        case IntersectProcessor::CmdTransientChop:
        case IntersectProcessor::CmdUndo:
        case IntersectProcessor::CmdRedo:
        case IntersectProcessor::CmdPanic:
        case IntersectProcessor::CmdSelectSlice:
        case IntersectProcessor::CmdSetRootNote:
        case IntersectProcessor::CmdStemSeparate:
            return true;
    }

    return false;
}

union FloatBits { float f; uint32_t u; };

uint64_t packPendingSliceParamPayload (int field, float value)
{
    FloatBits fb;
    fb.f = value;
    return (uint64_t) (uint32_t) field << 32
        | (uint64_t) fb.u;
}

void unpackPendingSliceParamPayload (uint64_t payload, int& field, float& value)
{
    field = (int) (payload >> 32);
    FloatBits fb;
    fb.u = (uint32_t) (payload & 0xFFFFFFFFu);
    value = fb.f;
}
} // namespace

void IntersectProcessor::pushCommand (Command cmd)
{
    const bool critical = isCriticalCommand (cmd.type);
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 > 0)
    {
        commandBuffer[(size_t) scope.startIndex1] = std::move (cmd);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }
    if (scope.blockSize2 > 0)
    {
        commandBuffer[(size_t) scope.startIndex2] = std::move (cmd);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    if (enqueueCoalescedCommand (cmd))
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    if (critical && enqueueOverflowCommand (std::move (cmd)))
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    droppedCommandCount.fetch_add (1, std::memory_order_relaxed);
    droppedCommandTotal.fetch_add (1, std::memory_order_relaxed);
    if (critical)
    {
        droppedCriticalCommandCount.fetch_add (1, std::memory_order_relaxed);
        droppedCriticalCommandTotal.fetch_add (1, std::memory_order_relaxed);
    }
}

bool IntersectProcessor::enqueueOverflowCommand (Command cmd)
{
    const int write = overflowWriteIndex.load (std::memory_order_relaxed);
    const int read = overflowReadIndex.load (std::memory_order_acquire);
    const int next = (write + 1) % kOverflowFifoSize;
    if (next == read)
        return false;

    overflowCommandBuffer[(size_t) write] = std::move (cmd);
    overflowWriteIndex.store (next, std::memory_order_release);
    return true;
}

void IntersectProcessor::drainOverflowCommands (bool& handledAny)
{
    for (;;)
    {
        const int read = overflowReadIndex.load (std::memory_order_relaxed);
        const int write = overflowWriteIndex.load (std::memory_order_acquire);
        if (read == write)
            break;

        handleCommand (overflowCommandBuffer[(size_t) read]);
        overflowReadIndex.store ((read + 1) % kOverflowFifoSize, std::memory_order_release);
        handledAny = true;
    }
}

bool IntersectProcessor::enqueueCoalescedCommand (const Command& cmd)
{
    if (! isCoalescableCommand (cmd.type))
        return false;

    if (cmd.type == CmdSetSliceParam)
    {
        pendingSetSliceParamPayload.store (packPendingSliceParamPayload (cmd.intParam1, cmd.floatParam1),
                                           std::memory_order_relaxed);
        pendingSetSliceParamIdx.store (cmd.sliceIdx, std::memory_order_relaxed);
        pendingSetSliceParam.store (true, std::memory_order_release);
        return true;
    }

    if (cmd.type == CmdSetSampleParam)
    {
        pendingSetSampleParamPayload.store (packPendingSliceParamPayload (cmd.intParam2, cmd.floatParam1),
                                            std::memory_order_relaxed);
        pendingSetSampleParamSampleId.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSampleParam.store (true, std::memory_order_release);
        return true;
    }

    if (cmd.type == CmdSetSliceBounds)
    {
        const int end = cmd.numPositions > 0 ? cmd.positions[0] : (int) cmd.floatParam1;
        pendingSetSliceBoundsSequence.fetch_add (1u, std::memory_order_acq_rel);
        pendingSetSliceBoundsIdx.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSliceBoundsStart.store (cmd.intParam2, std::memory_order_relaxed);
        pendingSetSliceBoundsEnd.store (end, std::memory_order_relaxed);
        pendingSetSliceBoundsSequence.fetch_add (1u, std::memory_order_release);
        pendingSetSliceBounds.store (true, std::memory_order_release);
        return true;
    }

    return false;
}

void IntersectProcessor::drainCoalescedCommands (bool& handledAny)
{
    if (pendingSetSliceBounds.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceBounds;
        for (;;)
        {
            const uint32_t startSeq = pendingSetSliceBoundsSequence.load (std::memory_order_acquire);
            if ((startSeq & 1u) != 0u)
                continue;

            const int idx = pendingSetSliceBoundsIdx.load (std::memory_order_relaxed);
            const int start = pendingSetSliceBoundsStart.load (std::memory_order_relaxed);
            const int end = pendingSetSliceBoundsEnd.load (std::memory_order_relaxed);
            const uint32_t endSeq = pendingSetSliceBoundsSequence.load (std::memory_order_acquire);

            if (startSeq == endSeq)
            {
                cmd.intParam1 = idx;
                cmd.intParam2 = start;
                cmd.positions[0] = end;
                cmd.numPositions = 1;
                handleCommand (cmd);
                handledAny = true;
                break;
            }
        }
    }

    if (pendingSetSliceParam.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceParam;
        unpackPendingSliceParamPayload (pendingSetSliceParamPayload.load (std::memory_order_relaxed),
                                        cmd.intParam1,
                                        cmd.floatParam1);
        cmd.sliceIdx = pendingSetSliceParamIdx.load (std::memory_order_relaxed);
        handleCommand (cmd);
        handledAny = true;
    }

    if (pendingSetSampleParam.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSampleParam;
        unpackPendingSliceParamPayload (pendingSetSampleParamPayload.load (std::memory_order_relaxed),
                                        cmd.intParam2,
                                        cmd.floatParam1);
        cmd.intParam1 = pendingSetSampleParamSampleId.load (std::memory_order_relaxed);
        handleCommand (cmd);
        handledAny = true;
    }
}

void IntersectProcessor::drainCommands()
{
    bool handledAny = false;

    drainOverflowCommands (handledAny);

    const auto scope = commandFifo.read (commandFifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex2 + i)]);

    if (scope.blockSize1 + scope.blockSize2 > 0)
        handledAny = true;

    drainCoalescedCommands (handledAny);

    if (pendingEndGesture.exchange (false, std::memory_order_acq_rel))
    {
        gestureSnapshotCaptured = false;
        blocksSinceGestureActivity = 0;
    }

    if (handledAny)
        uiSnapshotDirty.store (true, std::memory_order_release);

    const auto dropped = droppedCommandCount.exchange (0, std::memory_order_relaxed);
    const auto droppedCritical = droppedCriticalCommandCount.exchange (0, std::memory_order_relaxed);
    bool statusChanged = false;
    if (dropped > 0)
    {
        setDroppedCommandWarning (dropped, droppedCritical);
        statusChanged = true;
    }
    else if (handledAny)
    {
        statusChanged = clearDroppedCommandWarning();
    }

    if (statusChanged)
        uiSnapshotDirty.store (true, std::memory_order_release);

    if (handledAny || dropped > 0)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void IntersectProcessor::applyLiveDragBoundsToSlice()
{
    const int liveIdx = liveDragSliceIdx.load (std::memory_order_acquire);
    if (liveIdx >= 0 && liveIdx < sliceManager.getNumSlices())
    {
        const int maxLen = sampleData.getNumFrames();
        if (maxLen > 1)
        {
            auto& s = sliceManager.getSlice (liveIdx);
            int start = liveDragBoundsStart.load (std::memory_order_relaxed);
            int end   = liveDragBoundsEnd.load   (std::memory_order_relaxed);
            start = juce::jlimit (0, juce::jmax (0, maxLen - 1), start);
            end   = juce::jlimit (start + 1, juce::jmax (start + 1, maxLen), end);
            if (end - start < kMidiEditMinSliceLength)
                end = juce::jmin (maxLen, start + kMidiEditMinSliceLength);
            s.startSample = start;
            s.endSample   = end;
            syncSliceOwnershipFromAbsolute (s);
            const int sLen = end - start;
            s.loopStartOffset = juce::jlimit (0, juce::jmax (0, sLen - 1), s.loopStartOffset);
            if (s.loopLength > 0)
                s.loopLength = juce::jlimit (1, juce::jmax (1, sLen - s.loopStartOffset), s.loopLength);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }
}

UndoManager::Snapshot IntersectProcessor::makeSnapshot()
{
    UndoManager::Snapshot snap;
    const auto sampleSnap = sampleData.getSnapshot();
    if (sampleSnap != nullptr)
    {
        snap.numSessionSamples = juce::jmin ((int) sampleSnap->sessionSamples.size(),
                                             SampleData::kMaxSessionSamples);
        for (int i = 0; i < snap.numSessionSamples; ++i)
            snap.sessionSamples[(size_t) i] = sampleSnap->sessionSamples[(size_t) i];
    }
    snap.selectedSessionSampleId = selectedSessionSampleId.load (std::memory_order_relaxed);
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        snap.slices[(size_t) i] = sliceManager.getSlice (i);
    snap.numSlices = sliceManager.getNumSlices();
    snap.selectedSlice = sliceManager.selectedSlice;
    snap.rootNote = sliceManager.rootNote.load();

    snap.params = captureParamUndoState();
    snap.sampleParams = sampleParams;
    snap.midiSelectsSlice = midiSelectsSlice.load();
    snap.snapToZeroCrossing = snapToZeroCrossing.load();
    return snap;
}

void IntersectProcessor::captureSnapshot()
{
    undoMgr.push (makeSnapshot());
}

void IntersectProcessor::restoreSnapshot (const UndoManager::Snapshot& snap)
{
    // Audio thread: the reload is started on the message thread (LoadRequest, timerCallback).
    LoadRequest reload;
    reload.updatesPendingStateFile = true;
    for (int i = 0; i < juce::jmin (snap.numSessionSamples, SampleData::kMaxSessionSamples); ++i)
    {
        const auto& sample = snap.sessionSamples[(size_t) i];
        if (sample.filePath.isNotEmpty())
        {
            reload.paths[(size_t) reload.numSamples] = sample.filePath;
            reload.sampleIds[(size_t) reload.numSamples] = sample.sampleId;
            ++reload.numSamples;
        }
    }

    // Apply slice state immediately (safe — no allocation).
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i) = snap.slices[(size_t) i];
    sliceManager.setNumSlices (snap.numSlices);
    sliceManager.selectedSlice = snap.selectedSlice;
    sliceManager.rootNote.store (snap.rootNote);
    sampleParams = snap.sampleParams;
    selectedSessionSampleId.store (snap.selectedSessionSampleId, std::memory_order_relaxed);
    midiSelectsSlice.store (snap.midiSelectsSlice);
    snapToZeroCrossing.store (snap.snapToZeroCrossing);

    if (reload.numSamples > 0)
    {
        syncAllSliceAbsolutePositions();
        sliceManager.rebuildMidiMap();
        pendingStateRestoreToken.store (0, std::memory_order_release);
        reload.kind = LoadKindPreserveSlices;
        reload.token = beginSampleLoad (reload.kind);
    }
    else
    {
        clearVoicesBeforeSampleSwap();
        RetiredObjects unloaded;
        unloaded.sample = sampleData.clear();
        retire (std::move (unloaded));
        updateSampleMissingState();
        sliceManager.rebuildMidiMap();
    }
    loadRequests.push (std::move (reload));   // also clears the pending state file when there is no sample

    uiSnapshotDirty.store (true, std::memory_order_release);

    const int writeIndex = pendingParamRestoreIndex.load (std::memory_order_relaxed) == 0 ? 1 : 0;
    pendingParamRestoreStates[(size_t) writeIndex] = snap.params;
    pendingParamRestoreIndex.store (writeIndex, std::memory_order_release);
}

bool IntersectProcessor::applyDeferredParamRestore()
{
    const int pendingIndex = pendingParamRestoreIndex.exchange (-1, std::memory_order_acq_rel);
    if (pendingIndex < 0)
        return false;

    applyParamUndoState (pendingParamRestoreStates[(size_t) pendingIndex]);
    return true;
}

void IntersectProcessor::handleCommand (const Command& cmd)
{
    switch (cmd.type)
    {
        case CmdNone:
        case CmdLazyChopStart:
        case CmdLazyChopStop:
        case CmdFileLoadCompleted:
        case CmdFileLoadFailed:
        case CmdUndo:
        case CmdRedo:
        case CmdPanic:
        case CmdSelectSlice:
        case CmdStemSeparate:
            gestureSnapshotCaptured = false;
            break;

        case CmdBeginGesture:
            captureSnapshot();
            gestureSnapshotCaptured = true;
            blocksSinceGestureActivity = 0;
            break;

        case CmdSetSliceParam:
        case CmdSetSampleParam:
        case CmdSetRootNote:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = true;
            blocksSinceGestureActivity = 0;
            break;

        case CmdSetSliceBounds:
        case CmdCreateSlice:
        case CmdDeleteSlice:
        case CmdDeleteSessionSample:
        case CmdStretch:
        case CmdToggleLock:
        case CmdDuplicateSlice:
        case CmdSplitSlice:
        case CmdTransientChop:
        case CmdRepackMidi:
            if (getUiStatusMessage().source == UiStatusMessage::Source::midiLimit)
                clearUiStatusMessage();
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = false;
            blocksSinceGestureActivity = 0;
            break;
    }

    switch (cmd.type)
    {
        case CmdCreateSlice:
        {
            bool wasAtLimit = sliceManager.nextMidiNote() == kMaxMidiNote
                              && ! sliceManager.midiNoteToSlices (kMaxMidiNote).empty();
            int idx = sliceManager.createSlice (cmd.intParam1, cmd.intParam2);
            if (idx >= 0)
            {
                syncSliceOwnershipFromAbsolute (sliceManager.getSlice (idx));
                if (wasAtLimit)
                    setUiStatusMessage ("MIDI note limit - slice " + juce::String (idx + 1)
                        + " has no unique MIDI note",
                        true, UiStatusMessage::Source::midiLimit);
            }
            break;
        }

        case CmdDeleteSlice:
            sliceManager.deleteSlice (cmd.intParam1);
            break;

        case CmdDeleteSessionSample:
            deleteSessionSample (cmd.intParam1);
            break;

        case CmdLazyChopStart:
            if (sampleData.isLoaded())
            {
                const auto psp = makePreviewStretchParams (selectedSampleParams(), dawBpm.load(), currentSampleRate, &sampleData);
                lazyChop.start (sampleData.getNumFrames(), sliceManager, psp,
                                snapToZeroCrossing.load(), &sampleData.getBuffer());
            }
            break;

        case CmdLazyChopStop:
            lazyChop.stop (voicePool, sliceManager);
            break;

        case CmdStretch:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                const double timelineRate = sampleData.getDecodedSampleRate() > 0.0
                    ? sampleData.getDecodedSampleRate()
                    : currentSampleRate;
                float newBpm = GrainEngine::calcStretchBpm (
                    s.startSample, s.endSample, cmd.floatParam1, timelineRate);
                s.bpm = newBpm;
                s.lockMask |= kLockBpm;
            }
            break;
        }

        case CmdSetSampleParam:
            if (auto* params = sampleParams.find (cmd.intParam1))
                params->setField (cmd.intParam2, cmd.floatParam1);
            break;

        case CmdToggleLock:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                const auto& sampleLevel = paramsForSample (s.sampleId);
                uint64_t bit = cmd.lockBitParam;
                bool turningOn = !(s.lockMask & bit);

                if (turningOn)
                    SliceEdits::copySampleParam (s, sampleLevel, bit);

                s.lockMask ^= bit;
            }
            break;
        }

        case CmdSetSliceParam:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                if (SliceEdits::setField (s, paramsForSample (s.sampleId), cmd.intParam1, cmd.floatParam1))
                    sliceManager.rebuildMidiMap();
            }
            break;
        }

        case CmdSetSliceBounds:
        {
            int idx = cmd.intParam1;
            if (idx >= 0 && idx < sliceManager.getNumSlices())
            {
                const int maxLen = sampleData.getNumFrames();
                if (maxLen <= 1)
                    break;

                auto& s = sliceManager.getSlice (idx);
                int requestedEnd = (cmd.numPositions > 0) ? cmd.positions[0] : (int) cmd.floatParam1;
                int start = juce::jmin (cmd.intParam2, requestedEnd);
                int end = juce::jmax (cmd.intParam2, requestedEnd);
                start = juce::jlimit (0, juce::jmax (0, maxLen - 1), start);
                end = juce::jlimit (start + 1, juce::jmax (start + 1, maxLen), end);
                if (end - start < kMinSliceLengthSamples)
                    end = juce::jmin (maxLen, start + kMinSliceLengthSamples);
                s.startSample = start;
                s.endSample = end;
                syncSliceOwnershipFromAbsolute (s);
                // Clamp loop fields to new slice length
                const int newLen = end - start;
                s.loopStartOffset = juce::jlimit (0, juce::jmax (0, newLen - 1), s.loopStartOffset);
                if (s.loopLength > 0)
                    s.loopLength = juce::jlimit (1, juce::jmax (1, newLen - s.loopStartOffset), s.loopLength);
                sliceManager.rebuildMidiMap();
            }
            break;
        }

        case CmdDuplicateSlice:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                bool wasAtLimit = sliceManager.nextMidiNote() == kMaxMidiNote
                                  && ! sliceManager.midiNoteToSlices (kMaxMidiNote).empty();
                const auto& src = sliceManager.getSlice (sel);
                int newIdx = sliceManager.createSlice (src.startSample, src.endSample);
                if (newIdx >= 0)
                {
                    auto& dst = sliceManager.getSlice (newIdx);
                    int savedNote = dst.midiNote;  // assigned by createSlice
                    dst = src;                     // copy all params, lockMask, colour
                    dst.midiNote      = savedNote; // restore unique MIDI note
                    dst.highNote      = savedNote; // reset to single-note
                    dst.sliceRootNote = savedNote;
                    if (cmd.intParam1 >= 0)        // ctrl-drag: use explicit position
                    {
                        dst.startSample = cmd.intParam1;
                        dst.endSample   = cmd.intParam2;
                        syncSliceOwnershipFromAbsolute (dst);
                        // Clamp loop fields to new slice length
                        const int dLen = dst.endSample - dst.startSample;
                        dst.loopStartOffset = juce::jlimit (0, juce::jmax (0, dLen - 1), dst.loopStartOffset);
                        if (dst.loopLength > 0)
                            dst.loopLength = juce::jlimit (1, juce::jmax (1, dLen - dst.loopStartOffset), dst.loopLength);
                    }
                    else
                    {
                        dst.sampleId = src.sampleId;
                        dst.startInSample = src.startInSample;
                        dst.endInSample = src.endInSample;
                    }
                    // else (intParam1 == -1): inherit src.startSample/endSample as-is
                    sliceManager.selectedSlice = newIdx;
                    selectedSessionSampleId.store (dst.sampleId, std::memory_order_relaxed);
                    if (wasAtLimit)
                        setUiStatusMessage ("MIDI note limit - slice " + juce::String (newIdx + 1)
                            + " has no unique MIDI note",
                            true, UiStatusMessage::Source::midiLimit);
                }
            }
            break;
        }

        case CmdSplitSlice:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;
                int count = juce::jlimit (2, 128, cmd.intParam1);
                int len = endS - startS;
                // Notes for sub-slices start one past the highest note any existing
                // slice has, so no existing note is disturbed.
                int baseNote = sliceManager.nextMidiNote();

                sliceManager.deleteSlice (sel);

                bool doSnap = snapToZeroCrossing.load() && sampleData.isLoaded();
                int firstNew = -1;
                for (int i = 0; i < count; ++i)
                {
                    int s = startS + i * len / count;
                    int e = startS + (i + 1) * len / count;
                    if (doSnap)
                    {
                        if (i > 0)
                            s = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), s);
                        if (i < count - 1)
                            e = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), e);
                    }
                    if (e - s < kMinSliceLengthSamples) e = s + kMinSliceLengthSamples;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        dst.endSample   = e;
                        syncSliceOwnershipFromAbsolute (dst);
                        dst.midiNote      = juce::jlimit (0, kMaxMidiNote, baseNote + i);
                        dst.highNote      = dst.midiNote;
                        dst.sliceRootNote = dst.midiNote;
                        dst.colour      = savedColour;
                        dst.active      = true;
                        dst.loopStartOffset = 0;
                        dst.loopLength      = 0;
                    }
                    if (i == 0) firstNew = idx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                {
                    sliceManager.selectedSlice = firstNew;
                    selectedSessionSampleId.store (sliceManager.getSlice (firstNew).sampleId, std::memory_order_relaxed);
                }
                if (baseNote + count - 1 > kMaxMidiNote)
                {
                    int firstOverflow = firstNew + (kMaxMidiNote - baseNote + 1);
                    setUiStatusMessage ("MIDI note limit - slices " + juce::String (firstOverflow + 1)
                        + "+ have no unique MIDI note",
                        true, UiStatusMessage::Source::midiLimit);
                }
            }
            break;
        }

        case CmdTransientChop:
        {
            int sel = cmd.sliceIdx >= 0 ? cmd.sliceIdx : sliceManager.selectedSlice.load();
            if (sel >= 0 && sel < sliceManager.getNumSlices() && cmd.numPositions > 0)
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS = srcCopy.endSample;
                int baseNote = sliceManager.nextMidiNote();

                // Build fixed-size boundary list: [startS, ...positions..., endS]
                int bounds[SliceManager::kMaxSlices + 2];
                int numBounds = 0;
                bounds[numBounds++] = startS;
                for (int bi = 0; bi < cmd.numPositions; ++bi)
                    bounds[numBounds++] = cmd.positions[(size_t) bi];
                bounds[numBounds++] = endS;

                sliceManager.deleteSlice (sel);

                int firstNew = -1;
                int subIdx = 0;
                for (int i = 0; i + 1 < numBounds; ++i)
                {
                    int s = bounds[i];
                    int e = bounds[i + 1];
                    if (e - s < kMinSliceLengthSamples) continue;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        dst.endSample   = e;
                        syncSliceOwnershipFromAbsolute (dst);
                        dst.midiNote      = juce::jlimit (0, kMaxMidiNote, baseNote + subIdx);
                        dst.highNote      = dst.midiNote;
                        dst.sliceRootNote = dst.midiNote;
                        dst.colour      = savedColour;
                        dst.active      = true;
                        dst.loopStartOffset = 0;
                        dst.loopLength      = 0;
                    }
                    if (firstNew < 0) firstNew = idx;
                    ++subIdx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                {
                    sliceManager.selectedSlice = firstNew;
                    selectedSessionSampleId.store (sliceManager.getSlice (firstNew).sampleId, std::memory_order_relaxed);
                }
                if (baseNote + subIdx - 1 > kMaxMidiNote)
                {
                    int firstOverflow = firstNew + (kMaxMidiNote - baseNote + 1);
                    setUiStatusMessage ("MIDI note limit - slices " + juce::String (firstOverflow + 1)
                        + "+ have no unique MIDI note",
                        true, UiStatusMessage::Source::midiLimit);
                }
            }
            break;
        }

        case CmdRepackMidi:
        {
            int overflowAt = sliceManager.repackMidiNotes (cmd.intParam1 != 0);
            if (overflowAt >= 0)
                setUiStatusMessage ("MIDI note limit - slices " + juce::String (overflowAt + 1)
                    + "+ were not resequenced",
                    true, UiStatusMessage::Source::midiLimit);
            break;
        }

        case CmdFileLoadCompleted:
            jassertfalse; // Legacy path no longer used; completions arrive via completedLoadSuccess.
            break;

        case CmdFileLoadFailed:
            jassertfalse; // Legacy path no longer used; failures arrive via completedLoadFailure.
            break;

        case CmdUndo:
            if (undoMgr.canUndo())
                restoreSnapshot (undoMgr.undo (makeSnapshot()));
            break;

        case CmdRedo:
            if (undoMgr.canRedo())
                restoreSnapshot (undoMgr.redo());
            break;

        case CmdBeginGesture:
            break;

        case CmdPanic:
            voicePool.killAll();
            auditionPlayer.stopFromAudioThread();
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
            break;

        case CmdSelectSlice:
        {
            const int selected = juce::jlimit (-1, juce::jmax (-1, sliceManager.getNumSlices() - 1), cmd.intParam1);
            if (selected >= 0 && sliceManager.getSlice (selected).parked)
                break;
            sliceManager.selectedSlice.store (selected, std::memory_order_relaxed);
            if (selected >= 0 && selected < sliceManager.getNumSlices())
                selectedSessionSampleId.store (sliceManager.getSlice (selected).sampleId, std::memory_order_relaxed);
            break;
        }

        case CmdSetRootNote:
            sliceManager.rootNote.store (juce::jlimit (0, kMaxMidiNote, cmd.intParam1),
                                         std::memory_order_relaxed);
            break;

        case CmdStemSeparate:
            // Launched from message thread via startStemSeparation(); nothing to do here.
            break;

        case CmdNone:
            break;
    }
}
