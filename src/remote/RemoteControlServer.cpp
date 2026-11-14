#include "RemoteControlServer.h"
#include "RemoteParams.h"
#include "RemoteProtocol.h"
#include "../AppFiles.h"
#include "../PluginProcessor.h"
#include "../ui/IntersectLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
using Processor = IntersectProcessor;

const juce::String kPrefix ("/intersect/");
constexpr int kRemoteDragOwner = 3;   // IntersectProcessor::liveDragOwner: 0=none, 1=mouse, 2=midi, 3=remote

juce::int64 nowMs()
{
    return juce::Time::currentTimeMillis();
}

int intArg (const juce::OSCMessage& m, int index, int fallback = 0)
{
    if (index >= m.size())
        return fallback;
    const auto& a = m[index];
    if (a.isInt32())   return a.getInt32();
    if (a.isFloat32()) return juce::roundToInt (a.getFloat32());
    return fallback;
}

float floatArg (const juce::OSCMessage& m, int index, float fallback = 0.0f)
{
    if (index >= m.size())
        return fallback;
    const auto& a = m[index];
    if (a.isFloat32()) return a.getFloat32();
    if (a.isInt32())   return (float) a.getInt32();
    return fallback;
}

juce::String stringArg (const juce::OSCMessage& m, int index)
{
    if (index < m.size() && m[index].isString())
        return m[index].getString();
    return {};
}

/** Text form of a message, used to detect which state messages changed since the last send. */
juce::String fingerprint (const juce::OSCMessage& m)
{
    juce::String key = m.getAddressPattern().toString();
    for (const auto& a : m)
    {
        key << '|';
        if (a.isInt32())        key << a.getInt32();
        else if (a.isFloat32()) key << juce::String (a.getFloat32(), 6);
        else if (a.isString())  key << a.getString();
        else if (a.isBlob())    key << juce::String::toHexString (a.getBlob().getData(), (int) a.getBlob().getSize(), 0);
    }
    return key;
}

template <typename... Args>
juce::OSCMessage makeMessage (const char* address, Args&&... args)
{
    return juce::OSCMessage (juce::OSCAddressPattern (kPrefix + address), std::forward<Args> (args)...);
}

juce::String auditionStateName (AuditionStatus::State state)
{
    switch (state)
    {
        case AuditionStatus::State::idle:     return "idle";
        case AuditionStatus::State::decoding: return "decoding";
        case AuditionStatus::State::playing:  return "playing";
        case AuditionStatus::State::paused:   return "paused";
        case AuditionStatus::State::stopped:  return "stopped";
        case AuditionStatus::State::failed:   return "failed";
    }
    return "idle";
}

juce::String stemStateName (StemJobState state)
{
    switch (state)
    {
        case StemJobState::idle:       return "idle";
        case StemJobState::preparing:  return "preparing";
        case StemJobState::separating: return "separating";
        case StemJobState::writing:    return "writing";
        case StemJobState::importing:  return "importing";
        case StemJobState::cancelled:  return "cancelled";
        case StemJobState::failed:     return "failed";
        case StemJobState::completed:  return "completed";
    }
    return "idle";
}

std::vector<juce::File> audioFilesFrom (const juce::OSCMessage& m, int firstIndex)
{
    std::vector<juce::File> files;
    for (int i = firstIndex; i < m.size(); ++i)
    {
        if (! m[i].isString() || ! juce::File::isAbsolutePath (m[i].getString()))
            continue;
        const juce::File file (m[i].getString());
        if (file.existsAsFile() && AppFiles::isSupportedAudioFile (file))
            files.push_back (file);
    }
    return files;
}
} // namespace

//==============================================================================
class RemoteControlServer::OdfJob : public juce::Thread
{
public:
    OdfJob (SampleData::SnapshotPtr snapshotIn, int startIn, int endIn)
        : juce::Thread ("INTERSECT remote ODF"), snapshot (std::move (snapshotIn)), start (startIn), end (endIn) {}

    ~OdfJob() override { stopThread (2000); }

    void run() override
    {
        const double rate = snapshot->decodedSampleRate > 0.0 ? snapshot->decodedSampleRate : 44100.0;
        result = AudioAnalysis::computeSpectralFluxODF (snapshot->buffer, start, end, rate);
        done.store (true, std::memory_order_release);
    }

    SampleData::SnapshotPtr snapshot;   // keeps the buffer alive while the thread reads it
    const int start;
    const int end;
    AudioAnalysis::ODFResult result;
    std::atomic<bool> done { false };
};

//==============================================================================
RemoteControlServer::RemoteControlServer (IntersectProcessor& p)
    : processor (p),
      instanceId (juce::Uuid().toDashedString())
{
}

RemoteControlServer::~RemoteControlServer()
{
    stop();
}

bool RemoteControlServer::start()
{
    if (isRunning())
        return true;

    auto tryBind = [this] (int requestedPort) -> bool
    {
        auto candidate = std::make_unique<juce::DatagramSocket> (false);
        if (! candidate->bindToPort (requestedPort, "127.0.0.1"))
            return false;
        socket = std::move (candidate);
        port = socket->getBoundPort();
        return port > 0;
    };

    bool bound = false;
    for (int i = 0; i < RemoteProtocol::kNumFixedPorts && ! bound; ++i)
        bound = tryBind (RemoteProtocol::kFirstFixedPort + i);
    if (! bound)
        bound = tryBind (0);

    if (! bound || ! receiver.connectToSocket (*socket))
    {
        socket.reset();
        port = 0;
        return false;
    }

    // Replies leave from the same socket, so clients see them come from the server port.
    sender.connectToSocket (*socket, "127.0.0.1", port);
    receiver.addListener (this);
    registry.publish (instanceId, port, trackName);
    startTimerHz (RemoteProtocol::kStateTimerHz);
    return true;
}

void RemoteControlServer::stop()
{
    stopTimer();
    if (boundsGesture.sliceIdx >= 0)
        commitBoundsGesture();
    for (const auto& client : clients)
        send (client.port, makeMessage ("bye"));
    clients.clear();
    odfJob.reset();

    receiver.removeListener (this);
    receiver.disconnect();
    sender.disconnect();
    socket.reset();
    port = 0;
    registry.remove();
}

void RemoteControlServer::setTrackName (const juce::String& name)
{
    if (name == trackName)
        return;
    trackName = name;
    if (isRunning())
    {
        registry.publish (instanceId, port, trackName);
        for (const auto& client : clients)
            sendHelloAck (client.port);
    }
}

//==============================================================================
RemoteControlServer::Client* RemoteControlServer::findClient (int clientPort)
{
    for (auto& client : clients)
        if (client.port == clientPort)
            return &client;
    return nullptr;
}

void RemoteControlServer::touchClient (int clientPort)
{
    if (auto* client = findClient (clientPort))
        client->lastSeenMs = nowMs();
}

void RemoteControlServer::send (int clientPort, const juce::OSCMessage& message)
{
    if (clientPort > 0 && clientPort <= 65535)
        sender.sendToIPAddress ("127.0.0.1", clientPort, message);
}

void RemoteControlServer::broadcast (const juce::OSCMessage& message)
{
    for (const auto& client : clients)
        send (client.port, message);
}

//==============================================================================
void RemoteControlServer::oscMessageReceived (const juce::OSCMessage& m)
{
    const auto fullAddress = m.getAddressPattern().toString();
    if (! fullAddress.startsWith (kPrefix))
        return;

    const auto address = fullAddress.substring (kPrefix.length());

    if (address.startsWith ("slice/"))
        handleSlice (address, m);
    else if (address.startsWith ("sample/"))
        handleSample (address, m);
    else if (address.startsWith ("chop/"))
        handleChop (address, m);
    else if (address.startsWith ("files/") || address.startsWith ("audition/") || address.startsWith ("preset/"))
        handleBrowse (address, m);
    else
        handleSession (address, m);
}

void RemoteControlServer::handleSession (const juce::String& address, const juce::OSCMessage& m)
{
    if (address == "hello")
    {
        const auto name = stringArg (m, 0);
        const int clientPort = intArg (m, 1);
        if (clientPort <= 0)
            return;

        if (auto* existing = findClient (clientPort))
        {
            existing->name = name;
            existing->lastSeenMs = nowMs();
        }
        else
        {
            if ((int) clients.size() >= RemoteProtocol::kMaxClients)
                clients.erase (clients.begin());   // the oldest subscriber gives way
            clients.push_back ({ clientPort, name, nowMs() });
        }

        sendHelloAck (clientPort);
        publishState (true, clientPort);
        return;
    }

    if (address == "ping")
    {
        const int clientPort = intArg (m, 0);
        if (findClient (clientPort) != nullptr)
        {
            touchClient (clientPort);
            send (clientPort, makeMessage ("pong"));
        }
        else
        {
            // Unknown (e.g. the plugin was reloaded): ask the client to say hello again.
            send (clientPort, makeMessage ("bye"));
        }
        return;
    }

    if (address == "bye")
    {
        const int clientPort = intArg (m, 0);
        clients.erase (std::remove_if (clients.begin(), clients.end(),
                                       [clientPort] (const Client& c) { return c.port == clientPort; }),
                       clients.end());
        return;
    }

    if (address == "state/request")
    {
        publishState (true, intArg (m, 0));
        return;
    }

    if (address == "params/describe")
    {
        sendParamDescriptions (intArg (m, 0));
        return;
    }

    if (address == "view/peaks")
    {
        sendPeaks (intArg (m, 0), intArg (m, 1), intArg (m, 2), intArg (m, 3), intArg (m, 4));
        return;
    }

    Processor::Command cmd;

    if (address == "undo" || address == "redo")
    {
        commitBoundsGesture();
        cmd.type = address == "undo" ? Processor::CmdUndo : Processor::CmdRedo;
        processor.pushCommand (cmd);
    }
    else if (address == "gesture/begin")
    {
        cmd.type = Processor::CmdBeginGesture;
        processor.pushCommand (cmd);
    }
    else if (address == "gesture/end")
    {
        commitBoundsGesture();
        processor.pendingEndGesture.store (true, std::memory_order_release);
    }
    else if (address == "panic")
    {
        cmd.type = Processor::CmdPanic;
        processor.pushCommand (cmd);
    }
    else if (address == "root/set")
    {
        cmd.type = Processor::CmdSetRootNote;
        cmd.intParam1 = intArg (m, 0, kDefaultRootNote);
        processor.pushCommand (cmd);
    }
    else if (address == "midi/repack")
    {
        cmd.type = Processor::CmdRepackMidi;
        cmd.intParam1 = intArg (m, 0, 1) != 0 ? 1 : 0;
        processor.pushCommand (cmd);
    }
    else if (address == "toggle/followmidi")
    {
        processor.midiSelectsSlice.store (intArg (m, 0) != 0);
        processor.markUiSnapshotDirty();
    }
    else if (address == "toggle/snapzero")
    {
        processor.snapToZeroCrossing.store (intArg (m, 0) != 0);
        processor.markUiSnapshotDirty();
    }
    else if (address == "stems/separate")
    {
        const int sampleId = resolveSampleId (intArg (m, 0, -1));
        const auto models = processor.getInstalledStemModels();
        if (sampleId < 0 || models.empty())
        {
            processor.showTransientStatusMessage ("No stem model installed - open STEMS in the plugin first", true);
            return;
        }
        StemSelectionMask mask = (StemSelectionMask) intArg (m, 1, 0);
        if (mask == 0)
            for (int i = 0; i < 8; ++i)
                mask |= stemSelectionBitForIndex (i);
        const auto mode = intArg (m, 2, 0) != 0 ? StemExportMode::separate : StemExportMode::combine;
        processor.startStemSeparation (sampleId, models.front(), mask, mode);
    }
    else if (address == "stems/cancel")
    {
        processor.cancelStemSeparation();
    }
}

int RemoteControlServer::resolveSliceIndex (int requested) const
{
    const auto& ui = processor.getUiSliceSnapshot();
    const int idx = requested >= 0 ? requested : ui.selectedSlice;
    return (idx >= 0 && idx < ui.numSlices) ? idx : -1;
}

int RemoteControlServer::resolveSampleId (int requested) const
{
    const auto& ui = processor.getUiSliceSnapshot();
    const int id = requested >= 0 ? requested : ui.selectedSessionSampleId;
    for (int i = 0; i < ui.numSessionSamples; ++i)
        if (ui.sessionSamples[(size_t) i].sampleId == id)
            return id;
    return -1;
}

void RemoteControlServer::handleSlice (const juce::String& address, const juce::OSCMessage& m)
{
    Processor::Command cmd;

    if (address == "slice/select")
    {
        cmd.type = Processor::CmdSelectSlice;
        cmd.intParam1 = intArg (m, 0, -1);
        processor.pushCommand (cmd);
        return;
    }

    if (address == "slice/create")
    {
        const int start = intArg (m, 0);
        const int end = intArg (m, 1);
        if (end <= start)
            return;
        cmd.type = Processor::CmdCreateSlice;
        cmd.intParam1 = start;
        cmd.intParam2 = end;
        processor.pushCommand (cmd);
        return;
    }

    if (address == "slice/bounds")
    {
        const int idx = resolveSliceIndex (intArg (m, 0, -1));
        if (idx >= 0)
            updateBoundsGesture (idx, intArg (m, 1), intArg (m, 2));
        return;
    }

    const int idx = resolveSliceIndex (intArg (m, 0, -1));
    if (idx < 0)
        return;

    // Any structural edit to the slice list ends a pending bounds gesture first, so the
    // indices it refers to are still valid.
    if (address != "slice/param")
        commitBoundsGesture();

    if (address == "slice/delete")
    {
        cmd.type = Processor::CmdDeleteSlice;
        cmd.intParam1 = idx;
    }
    else if (address == "slice/duplicate")
    {
        cmd.type = Processor::CmdDuplicateSlice;
        cmd.intParam1 = -1;   // same position as the source
        cmd.sliceIdx = idx;
    }
    else if (address == "slice/split")
    {
        cmd.type = Processor::CmdSplitSlice;
        cmd.intParam1 = juce::jlimit (2, 128, intArg (m, 1, 2));
        cmd.sliceIdx = idx;
    }
    else if (address == "slice/param")
    {
        const int field = intArg (m, 1, -1);
        const auto* info = RemoteParams::find (field);
        if (info == nullptr || (info->levels & RemoteParams::kLevelSlice) == 0)
            return;
        const float value = juce::jlimit (info->minValue, info->maxValue, floatArg (m, 2));
        cmd.type = Processor::CmdSetSliceParam;
        cmd.intParam1 = field;
        cmd.floatParam1 = RemoteParams::toSliceUnits (field, value);
        cmd.sliceIdx = idx;
    }
    else if (address == "slice/lock")
    {
        const auto* info = RemoteParams::find (intArg (m, 1, -1));
        if (info == nullptr || info->lockBit == 0 || (info->levels & RemoteParams::kLevelSample) == 0)
            return;
        cmd.type = Processor::CmdToggleLock;
        cmd.lockBitParam = info->lockBit;
        cmd.sliceIdx = idx;
    }
    else if (address == "slice/setbpm")
    {
        cmd.type = Processor::CmdStretch;
        cmd.floatParam1 = juce::jlimit (0.0625f, 64.0f, floatArg (m, 1, 1.0f));
        cmd.sliceIdx = idx;
    }
    else
    {
        return;
    }

    processor.pushCommand (cmd);
}

void RemoteControlServer::handleSample (const juce::String& address, const juce::OSCMessage& m)
{
    if (address == "sample/load")
    {
        // sample/load <append 0|1> <path> [<path> ...]
        const bool append = intArg (m, 0) != 0 && processor.sampleData.isLoaded();
        const auto files = audioFilesFrom (m, 1);
        if (files.empty())
            return;
        commitBoundsGesture();
        processor.stopAudition();
        processor.enqueueUiUndoSnapshot();
        processor.loadFilesAsync (files, append);
        if (! append)
        {
            processor.zoom.store (1.0f);
            processor.scroll.store (0.0f);
        }
        return;
    }

    if (address == "sample/relink")
    {
        const auto files = audioFilesFrom (m, 0);
        if (! files.empty())
            processor.relinkFileAsync (files.front());
        return;
    }

    const int sampleId = resolveSampleId (intArg (m, 0, -1));
    if (sampleId < 0)
        return;

    if (address == "sample/select")
    {
        processor.selectedSessionSampleId.store (sampleId, std::memory_order_relaxed);
        processor.markUiSnapshotDirty();
    }
    else if (address == "sample/delete")
    {
        commitBoundsGesture();
        processor.deleteSessionSampleAsync (sampleId);
    }
    else if (address == "sample/reorder")
    {
        commitBoundsGesture();
        processor.reorderSessionSampleAsync (sampleId, intArg (m, 1));
    }
    else if (address == "sample/param")
    {
        const int field = intArg (m, 1, -1);
        const auto* info = RemoteParams::find (field);
        if (info == nullptr || (info->levels & RemoteParams::kLevelSample) == 0)
            return;
        Processor::Command cmd;
        cmd.type = Processor::CmdSetSampleParam;
        cmd.intParam1 = sampleId;
        cmd.intParam2 = field;
        cmd.floatParam1 = juce::jlimit (info->minValue, info->maxValue, floatArg (m, 2));
        processor.pushCommand (cmd);
    }
    else if (address == "sample/setbpm")
    {
        processor.selectedSessionSampleId.store (sampleId, std::memory_order_relaxed);
        processor.setBpmFromBars (false, juce::jlimit (0.0625f, 64.0f, floatArg (m, 1, 1.0f)));
    }
}

void RemoteControlServer::handleChop (const juce::String& address, const juce::OSCMessage& m)
{
    Processor::Command cmd;

    if (address == "chop/lazy")
    {
        commitBoundsGesture();
        cmd.type = intArg (m, 0) != 0 ? Processor::CmdLazyChopStart : Processor::CmdLazyChopStop;
        processor.pushCommand (cmd);
        return;
    }

    if (address == "chop/analyze")
    {
        const int idx = resolveSliceIndex (intArg (m, 0, -1));
        if (m.size() > 1) chopSensitivity = juce::jlimit (0.0f, 100.0f, floatArg (m, 1, chopSensitivity));
        if (m.size() > 2) chopMinMs = juce::jlimit (20.0f, 500.0f, floatArg (m, 2, chopMinMs));
        if (idx >= 0)
            startAutoChopAnalysis (idx);
        return;
    }

    if (address == "chop/settings")
    {
        chopSensitivity = juce::jlimit (0.0f, 100.0f, floatArg (m, 0, chopSensitivity));
        chopMinMs = juce::jlimit (20.0f, 500.0f, floatArg (m, 1, chopMinMs));
        chopPreviewDirty = true;
        return;
    }

    if (address == "chop/cancel")
    {
        odfJob.reset();
        odfReady = false;
        odfSliceIdx = -1;
        chopPositions.clear();
        broadcast (makeMessage ("chop/preview", -1, juce::MemoryBlock()));
        return;
    }

    if (address == "chop/apply")
    {
        commitBoundsGesture();
        if (! odfReady || odfSliceIdx < 0 || chopPositions.empty())
            return;

        // The slice list may have changed since the analysis; never chop a different slice.
        const auto& ui = processor.getUiSliceSnapshot();
        if (odfSliceIdx >= ui.numSlices
            || ui.slices[(size_t) odfSliceIdx].startSample != odfStart
            || ui.slices[(size_t) odfSliceIdx].endSample != odfEnd)
        {
            processor.showTransientStatusMessage ("Slice changed - analyze it again before chopping", true);
            return;
        }

        cmd.type = Processor::CmdTransientChop;
        cmd.sliceIdx = odfSliceIdx;
        for (int pos : chopPositions)
            if (cmd.numPositions < (int) cmd.positions.size())
                cmd.positions[(size_t) cmd.numPositions++] = pos;
        processor.pushCommand (cmd);

        odfReady = false;
        odfSliceIdx = -1;
        chopPositions.clear();
        broadcast (makeMessage ("chop/preview", -1, juce::MemoryBlock()));
    }
}

void RemoteControlServer::handleBrowse (const juce::String& address, const juce::OSCMessage& m)
{
    if (address == "files/list")
    {
        const auto path = stringArg (m, 2);
        sendFileList (intArg (m, 0), intArg (m, 1),
                      juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File());
    }
    else if (address == "audition/play")
    {
        const auto path = stringArg (m, 0);
        if (juce::File::isAbsolutePath (path) && juce::File (path).existsAsFile())
            processor.auditionFileAsync (juce::File (path));
    }
    else if (address == "audition/stop")
    {
        processor.stopAudition();
    }
    else if (address == "preset/load")
    {
        const auto path = stringArg (m, 0);
        if (! juce::File::isAbsolutePath (path))
            return;
        const juce::File preset (path);
        if (! preset.existsAsFile() || ! AppFiles::isPresetFile (preset))
            return;
        commitBoundsGesture();
        processor.stopAudition();
        processor.loadPresetAsync (preset);
    }
    else if (address == "preset/save")
    {
        // No keyboard on most controllers: without a path, save into the preset library under
        // the first sample's name, numbered like the editor's SAVE.
        auto path = stringArg (m, 0);
        juce::File target;
        if (juce::File::isAbsolutePath (path))
        {
            target = juce::File (path).withFileExtension (AppFiles::kPresetExtension);
        }
        else
        {
            const auto folder = AppFiles::getPresetsDir();
            if (! folder.createDirectory())
                return;
            const auto& ui = processor.getUiSliceSnapshot();
            const auto source = ui.numSessionSamples > 0 ? ui.sessionSamples[0].fileName.toString()
                                                         : ui.sampleFileName.toString();
            auto baseName = juce::File::createLegalFileName (source.upToLastOccurrenceOf (".", false, false).trim());
            if (baseName.isEmpty())
                baseName = "Untitled";
            target = folder.getChildFile (baseName + AppFiles::kPresetExtension);
            for (int n = 2; target.exists(); ++n)
                target = folder.getChildFile (baseName + " " + juce::String (n) + AppFiles::kPresetExtension);
        }
        processor.savePresetAsync (target, false);
    }
}

//==============================================================================
void RemoteControlServer::updateBoundsGesture (int sliceIdx, int start, int end)
{
    if (boundsGesture.sliceIdx >= 0 && boundsGesture.sliceIdx != sliceIdx)
        commitBoundsGesture();

    // Same ownership protocol as the editor's mouse drags and the NRPN encoders. Ownership can
    // be reset under us (undo, state restore), so it is re-checked on every update.
    int expectedOwner = 0;
    if (! processor.liveDragOwner.compare_exchange_strong (expectedOwner, kRemoteDragOwner,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire)
        && expectedOwner != kRemoteDragOwner)
    {
        return;   // the mouse or NRPN is editing right now
    }

    if (boundsGesture.sliceIdx < 0)
    {
        const auto& s = processor.getUiSliceSnapshot().slices[(size_t) sliceIdx];
        boundsGesture.sliceIdx = sliceIdx;
        boundsGesture.originalStart = s.startSample;
        boundsGesture.originalEnd = s.endSample;

        Processor::Command gestureCmd;
        gestureCmd.type = Processor::CmdBeginGesture;
        processor.pushCommand (gestureCmd);
    }

    const int total = processor.sampleData.getNumFrames();
    start = juce::jlimit (0, juce::jmax (0, total - 1), start);
    end = juce::jlimit (start + 1, juce::jmax (start + 1, total), end);

    boundsGesture.start = start;
    boundsGesture.end = end;
    boundsGesture.lastEditMs = nowMs();

    processor.liveDragBoundsStart.store (start, std::memory_order_relaxed);
    processor.liveDragBoundsEnd.store (end, std::memory_order_relaxed);
    processor.liveDragSliceIdx.store (sliceIdx, std::memory_order_release);
}

void RemoteControlServer::commitBoundsGesture()
{
    if (boundsGesture.sliceIdx < 0)
        return;

    int start = boundsGesture.start;
    int end = boundsGesture.end;
    if (processor.snapToZeroCrossing.load())
    {
        if (auto snap = processor.sampleData.getSnapshot())
        {
            if (start != boundsGesture.originalStart)
                start = AudioAnalysis::findNearestZeroCrossing (snap->buffer, start);
            if (end != boundsGesture.originalEnd)
                end = AudioAnalysis::findNearestZeroCrossing (snap->buffer, end);
        }
    }

    Processor::Command cmd;
    cmd.type = Processor::CmdSetSliceBounds;
    cmd.intParam1 = boundsGesture.sliceIdx;
    cmd.intParam2 = start;
    cmd.positions[0] = end;
    cmd.numPositions = 1;
    processor.pushCommand (cmd);

    // Release only our own ownership; the mouse may have taken over after a reset.
    int expectedOwner = kRemoteDragOwner;
    if (processor.liveDragOwner.compare_exchange_strong (expectedOwner, 0, std::memory_order_acq_rel))
        processor.liveDragSliceIdx.store (-1, std::memory_order_release);
    processor.pendingEndGesture.store (true, std::memory_order_release);
    boundsGesture = {};
}

void RemoteControlServer::startAutoChopAnalysis (int sliceIdx)
{
    const auto snapshot = processor.sampleData.getSnapshot();
    if (snapshot == nullptr)
        return;

    const auto& s = processor.getUiSliceSnapshot().slices[(size_t) sliceIdx];
    odfJob.reset();
    odfReady = false;
    chopPositions.clear();
    odfSliceIdx = sliceIdx;
    odfStart = s.startSample;
    odfEnd = s.endSample;
    odfJob = std::make_unique<OdfJob> (snapshot, odfStart, odfEnd);
    odfJob->startThread (juce::Thread::Priority::background);
    broadcast (makeMessage ("chop/analyzing", sliceIdx));
}

void RemoteControlServer::publishChopPreview()
{
    if (odfJob != nullptr && odfJob->done.load (std::memory_order_acquire))
    {
        odf = std::move (odfJob->result);
        odfJob.reset();
        odfReady = true;
        chopPreviewDirty = true;
    }

    if (! odfReady || ! chopPreviewDirty)
        return;
    chopPreviewDirty = false;

    const auto snapshot = processor.sampleData.getSnapshot();
    if (snapshot == nullptr)
        return;

    const double rate = snapshot->decodedSampleRate > 0.0 ? snapshot->decodedSampleRate : 44100.0;
    chopPositions = AudioAnalysis::pickAutoChopPositions (odf, snapshot->buffer, chopSensitivity, rate, chopMinMs,
                                                          processor.snapToZeroCrossing.load(), odfStart, odfEnd);

    juce::MemoryBlock blob;
    for (int pos : chopPositions)
    {
        const auto le = juce::ByteOrder::swapIfBigEndian ((juce::uint32) pos);
        blob.append (&le, sizeof (le));
    }
    broadcast (makeMessage ("chop/preview", odfSliceIdx, blob));
}

//==============================================================================
void RemoteControlServer::timerCallback()
{
    juce::String hostTrackName;
    if (processor.takeHostTrackNameIfChanged (hostTrackName))
        setTrackName (hostTrackName);

    const auto now = nowMs();
    clients.erase (std::remove_if (clients.begin(), clients.end(),
                                   [now] (const Client& c) { return now - c.lastSeenMs > RemoteProtocol::kClientTimeoutMs; }),
                   clients.end());

    if (boundsGesture.sliceIdx >= 0 && now - boundsGesture.lastEditMs > RemoteProtocol::kBoundsCommitIdleMs)
        commitBoundsGesture();

    if (clients.empty())
    {
        sent = {};
        return;
    }

    publishState (false, 0);
    publishPlayheads();
    publishChopPreview();
}

void RemoteControlServer::sendHelloAck (int clientPort)
{
    send (clientPort, makeMessage ("hello/ack", RemoteProtocol::kVersion, instanceId,
                                   juce::String (JucePlugin_VersionString), trackName));
}

void RemoteControlServer::sendParamDescriptions (int clientPort)
{
    const auto& infos = RemoteParams::all();
    for (const auto& info : infos)
    {
        send (clientPort, makeMessage ("param/info", info.field, juce::String (info.name), juce::String (info.group),
                                       juce::String (info.unit), info.minValue, info.maxValue, info.step,
                                       juce::String (info.choices), info.levels, info.lockBit != 0 ? 1 : 0));
    }
    send (clientPort, makeMessage ("param/info/end", (int) infos.size()));
}

void RemoteControlServer::publishState (bool forceAll, int onlyClientPort)
{
    if (onlyClientPort > 0 && findClient (onlyClientPort) == nullptr)
        return;

    const uint32_t version = processor.getUiSliceSnapshotVersion();
    const auto audition = processor.pollAuditionStatus();
    const auto& ui = processor.getUiSliceSnapshot();

    // Groups of state messages; each is compared with what was sent last time.
    std::vector<juce::OSCMessage> global, samples, slices, sampleParams, sliceParams;

    global.push_back (makeMessage ("state/global",
                                   ui.numSlices, ui.selectedSlice, ui.selectedSessionSampleId, ui.rootNote,
                                   ui.sampleLoaded ? 1 : 0, ui.sampleMissing ? 1 : 0,
                                   processor.sampleData.getNumFrames(), (float) ui.sampleSampleRate,
                                   ui.numSessionSamples,
                                   processor.lazyChop.isActive() ? 1 : 0,
                                   processor.midiSelectsSlice.load() ? 1 : 0,
                                   processor.snapToZeroCrossing.load() ? 1 : 0));

    for (int i = 0; i < ui.numSessionSamples; ++i)
    {
        const auto& smp = ui.sessionSamples[(size_t) i];
        samples.push_back (makeMessage ("state/sample", i, smp.sampleId, smp.startSample, smp.numFrames,
                                        smp.fileName.toString()));
    }

    for (int i = 0; i < ui.numSlices; ++i)
    {
        const auto& s = ui.slices[(size_t) i];
        slices.push_back (makeMessage ("state/slice", i, s.sampleId, s.startSample, s.endSample,
                                       s.midiNote, s.highNote, s.sliceRootNote,
                                       (int) s.colour.getARGB(),
                                       (int) (juce::uint32) (s.lockMask & 0xffffffffull),
                                       (int) (juce::uint32) (s.lockMask >> 32)));
    }

    const int selectedSampleId = ui.selectedSessionSampleId;
    const auto& selectedSampleParams = ui.sampleParamsFor (selectedSampleId);
    for (const auto& info : RemoteParams::all())
    {
        if ((info.levels & RemoteParams::kLevelSample) != 0 && selectedSampleId >= 0)
            sampleParams.push_back (makeMessage ("state/param", juce::String ("sample"), selectedSampleId, info.field,
                                                 selectedSampleParams.getField (info.field), 0));
    }

    if (ui.selectedSlice >= 0 && ui.selectedSlice < ui.numSlices)
    {
        const auto& s = ui.slices[(size_t) ui.selectedSlice];
        const auto& owner = ui.sampleParamsFor (s.sampleId);
        for (const auto& info : RemoteParams::all())
        {
            if ((info.levels & RemoteParams::kLevelSlice) == 0)
                continue;
            bool locked = false;
            const float value = RemoteParams::resolveSliceField (s, owner, info.field, locked);
            sliceParams.push_back (makeMessage ("state/param", juce::String ("slice"), ui.selectedSlice, info.field,
                                                value, locked ? 1 : 0));
        }
    }

    const auto status = makeMessage ("state/status", ui.hasStatusMessage ? ui.statusMessage.toString() : juce::String(),
                                     ui.statusIsWarning ? 1 : 0);
    const auto stems = makeMessage ("state/stems", stemStateName (ui.stemJobState), ui.stemJobProgress,
                                    ui.stemJobSourceSampleId);
    const auto auditionMsg = makeMessage ("state/audition", auditionStateName (audition.state), audition.position,
                                          audition.file.getFullPathName());

    // The editor's current theme, so controllers can draw and light in the same colours.
    // Theme loading lives in the editor: before it first opens, this is the built-in default.
    const auto& theme = getTheme();
    juce::StringArray themeKeys;
    juce::MemoryBlock themeColours;
    auto addColour = [&themeKeys, &themeColours] (const juce::String& key, juce::Colour colour)
    {
        themeKeys.add (key);
        const auto le = juce::ByteOrder::swapIfBigEndian (colour.getARGB());
        themeColours.append (&le, sizeof (le));
    };
    const juce::Colour* surfaces[] = { &theme.surface0, &theme.surface1, &theme.surface2,
                                       &theme.surface3, &theme.surface4, &theme.surface5 };
    for (int i = 0; i < 6; ++i)
        addColour ("surface" + juce::String (i), *surfaces[i]);
    addColour ("text0", theme.text0);
    addColour ("text1", theme.text1);
    addColour ("text2", theme.text2);
    addColour ("waveform", theme.waveform);
    addColour ("accent", theme.accent);
    const juce::Colour* semantic[] = { &theme.color1, &theme.color2, &theme.color3, &theme.color4, &theme.color5 };
    for (int i = 0; i < 5; ++i)
        addColour ("color" + juce::String (i + 1), *semantic[i]);
    for (int i = 0; i < 16; ++i)
        addColour ("slice" + juce::String (i), theme.slicePalette[i]);
    const auto themeMsg = makeMessage ("state/theme", theme.name, themeKeys.joinIntoString (";"), themeColours);

    // Single-client resend (hello / state/request): send everything to that client only.
    if (onlyClientPort > 0)
    {
        for (const auto* group : { &global, &samples, &slices, &sampleParams, &sliceParams })
            for (const auto& msg : *group)
                send (onlyClientPort, msg);
        send (onlyClientPort, status);
        send (onlyClientPort, stems);
        send (onlyClientPort, auditionMsg);
        send (onlyClientPort, themeMsg);
        send (onlyClientPort, makeMessage ("state/sync", (int) version));
        return;
    }

    bool anySent = false;
    auto sendChanged = [this, forceAll, &anySent] (const std::vector<juce::OSCMessage>& now,
                                                   std::vector<juce::String>& before)
    {
        std::vector<juce::String> keys;
        keys.reserve (now.size());
        for (size_t i = 0; i < now.size(); ++i)
        {
            keys.push_back (fingerprint (now[i]));
            if (forceAll || ! sent.valid || i >= before.size() || before[i] != keys.back())
            {
                broadcast (now[i]);
                anySent = true;
            }
        }
        if (now.size() != before.size())
            anySent = true;   // a shorter list is announced through state/global counts
        before = std::move (keys);
    };

    auto sendIfChanged = [this, forceAll, &anySent] (const juce::OSCMessage& msg, juce::String& before)
    {
        auto key = fingerprint (msg);
        if (forceAll || ! sent.valid || key != before)
        {
            broadcast (msg);
            anySent = true;
            before = std::move (key);
        }
    };

    if (forceAll || version != lastSnapshotVersion || ! sent.valid)
    {
        sendChanged (global, sent.global);
        sendChanged (samples, sent.samples);
        sendChanged (slices, sent.slices);
        sendChanged (sampleParams, sent.sampleParams);
        sendChanged (sliceParams, sent.sliceParams);
    }
    else
    {
        // Toggles and lazy-chop state are not part of the snapshot version.
        sendChanged (global, sent.global);
    }

    sendIfChanged (status, sent.status);
    sendIfChanged (stems, sent.stems);
    sendIfChanged (auditionMsg, sent.audition);
    sendIfChanged (themeMsg, sent.theme);

    sent.valid = true;
    lastSnapshotVersion = version;
    if (anySent)
        broadcast (makeMessage ("state/sync", (int) version));
}

void RemoteControlServer::publishPlayheads()
{
    juce::MemoryBlock blob;
    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
    {
        const float pos = processor.voicePool.voicePositions[(size_t) i].load (std::memory_order_relaxed);
        if (pos > 0.0f)
        {
            const auto le = juce::ByteOrder::swapIfBigEndian ((juce::uint32) juce::roundToInt (pos));
            blob.append (&le, sizeof (le));
        }
    }

    if (blob == sent.playheads)
        return;
    sent.playheads = blob;
    const int chopPos = processor.lazyChop.isActive() ? processor.lazyChop.getChopPos() : -1;
    broadcast (makeMessage ("playheads", blob, chopPos));
}

void RemoteControlServer::sendPeaks (int clientPort, int requestId, int startFrame, int endFrame, int width)
{
    const auto snapshot = processor.sampleData.getSnapshot();
    const int total = snapshot != nullptr ? snapshot->buffer.getNumSamples() : 0;
    width = juce::jlimit (1, RemoteProtocol::kMaxPeakWidth, width);
    startFrame = juce::jlimit (0, juce::jmax (0, total - 1), startFrame);
    endFrame = juce::jlimit (startFrame + 1, juce::jmax (startFrame + 1, total), endFrame);

    juce::MemoryBlock blob ((size_t) width * 2, true);
    if (total > 0)
    {
        auto* out = static_cast<juce::int8*> (blob.getData());
        const double framesPerColumn = (double) (endFrame - startFrame) / (double) width;

        // Coarsest mipmap that still has at least one peak per column.
        const SampleData::PeakMipmap* level = nullptr;
        for (const auto& m : snapshot->peakMipmaps)
            if (m.samplesPerPeak > 0 && (double) m.samplesPerPeak <= framesPerColumn && ! m.maxPeaks.empty())
                level = &m;

        const float* left = snapshot->buffer.getReadPointer (0);
        const float* right = snapshot->buffer.getNumChannels() > 1 ? snapshot->buffer.getReadPointer (1) : left;
        auto toInt8 = [] (float v) { return (juce::int8) juce::jlimit (-127, 127, juce::roundToInt (v * 127.0f)); };

        for (int col = 0; col < width; ++col)
        {
            const int f0 = startFrame + (int) std::floor (col * framesPerColumn);
            const int f1 = juce::jmax (f0 + 1, juce::jmin (endFrame, startFrame + (int) std::floor ((col + 1) * framesPerColumn)));
            float lo = 0.0f, hi = 0.0f;

            if (level != nullptr)
            {
                const int numPeaks = (int) level->maxPeaks.size();
                const int i0 = juce::jlimit (0, numPeaks - 1, f0 / level->samplesPerPeak);
                const int i1 = juce::jlimit (i0 + 1, numPeaks, (f1 + level->samplesPerPeak - 1) / level->samplesPerPeak);
                lo = level->minPeaks[(size_t) i0];
                hi = level->maxPeaks[(size_t) i0];
                for (int i = i0 + 1; i < i1; ++i)
                {
                    lo = std::min (lo, level->minPeaks[(size_t) i]);
                    hi = std::max (hi, level->maxPeaks[(size_t) i]);
                }
            }
            else
            {
                lo = hi = left[f0];
                for (int f = f0; f < f1 && f < total; ++f)
                {
                    lo = std::min ({ lo, left[f], right[f] });
                    hi = std::max ({ hi, left[f], right[f] });
                }
            }

            out[col * 2] = toInt8 (lo);
            out[col * 2 + 1] = toInt8 (hi);
        }
    }

    send (clientPort, makeMessage ("peaks", requestId, startFrame, endFrame, blob));
}

void RemoteControlServer::sendFileList (int clientPort, int requestId, const juce::File& dir)
{
    constexpr int kMaxEntries = 2000;
    int count = 0;
    auto sendEntry = [&] (const juce::String& kind, const juce::String& name, const juce::File& file)
    {
        send (clientPort, makeMessage ("files/entry", requestId, count++, kind, name, file.getFullPathName()));
    };

    if (dir == juce::File() || ! dir.isDirectory())
    {
        // Top level: the places a controller user is most likely to want.
        sendEntry ("dir", "Presets", AppFiles::getPresetsDir());
        for (auto [name, location] : { std::pair { "Home",      juce::File::userHomeDirectory },
                                       std::pair { "Music",     juce::File::userMusicDirectory },
                                       std::pair { "Desktop",   juce::File::userDesktopDirectory },
                                       std::pair { "Documents", juce::File::userDocumentsDirectory } })
        {
            const auto folder = juce::File::getSpecialLocation (location);
            if (folder.isDirectory())
                sendEntry ("dir", name, folder);
        }
        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots (roots);
        for (const auto& root : roots)
            sendEntry ("dir", root.getFullPathName(), root);
        send (clientPort, makeMessage ("files/end", requestId, count, juce::String(), juce::String()));
        return;
    }

    auto byName = [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    };

    auto folders = dir.findChildFiles (juce::File::findDirectories, false);
    auto files = dir.findChildFiles (juce::File::findFiles, false);
    std::sort (folders.begin(), folders.end(), byName);
    std::sort (files.begin(), files.end(), byName);

    for (const auto& folder : folders)
        if (! folder.isHidden() && count < kMaxEntries)
            sendEntry ("dir", folder.getFileName(), folder);

    for (const auto& file : files)
    {
        if (file.isHidden() || count >= kMaxEntries)
            continue;
        if (AppFiles::isPresetFile (file))
            sendEntry ("preset", file.getFileNameWithoutExtension(), file);
        else if (AppFiles::isSupportedAudioFile (file))
            sendEntry ("audio", file.getFileName(), file);
    }

    const auto parent = dir.getParentDirectory();
    send (clientPort, makeMessage ("files/end", requestId, count, dir.getFullPathName(),
                                   parent != dir ? parent.getFullPathName() : juce::String()));
}
