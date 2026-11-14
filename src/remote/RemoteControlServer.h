#pragma once

#include <juce_osc/juce_osc.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "../audio/AudioAnalysis.h"
#include "../audio/SampleData.h"
#include "../audio/Slice.h"
#include "../params/SampleParams.h"
#include "InstanceRegistry.h"

class IntersectProcessor;

/** Lets external controllers (Pushtoo, a Maschine app, TouchOSC...) drive INTERSECT over OSC.

    Lives on the message thread: OSC messages arrive through a MessageLoopCallback listener,
    so commands go through the processor's single-producer pushCommand() exactly like the
    editor's do. A 30 Hz timer streams state deltas, playheads and auto-chop previews back to
    every subscribed client. Owned by the processor, so it works with the editor closed.

    Protocol: docs/remote-control-protocol.md.
*/
class RemoteControlServer : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                            private juce::Timer
{
public:
    explicit RemoteControlServer (IntersectProcessor& processor);
    ~RemoteControlServer() override;

    bool start();
    void stop();
    bool isRunning() const noexcept { return port > 0; }
    int getPort() const noexcept { return port; }

    /** Host track name (from AudioProcessor::updateTrackProperties), shown by controllers. */
    void setTrackName (const juce::String& name);

private:
    struct Client
    {
        int port = 0;
        juce::String name;
        juce::int64 lastSeenMs = 0;
    };

    // Last values sent, so the timer only broadcasts what changed.
    struct SentState
    {
        bool valid = false;
        std::vector<juce::String> global;
        std::vector<juce::String> samples;
        std::vector<juce::String> slices;
        std::vector<juce::String> sampleParams;
        std::vector<juce::String> sliceParams;
        juce::String status;
        juce::String stems;
        juce::String audition;
        juce::String theme;
        juce::MemoryBlock playheads;
    };

    struct BoundsGesture
    {
        int sliceIdx = -1;
        int originalStart = 0;
        int originalEnd = 0;
        int start = 0;
        int end = 0;
        juce::int64 lastEditMs = 0;
    };

    class OdfJob;

    void oscMessageReceived (const juce::OSCMessage& message) override;
    void timerCallback() override;

    void handleSession (const juce::String& address, const juce::OSCMessage& m);
    void handleSlice (const juce::String& address, const juce::OSCMessage& m);
    void handleSample (const juce::String& address, const juce::OSCMessage& m);
    void handleChop (const juce::String& address, const juce::OSCMessage& m);
    void handleBrowse (const juce::String& address, const juce::OSCMessage& m);

    Client* findClient (int clientPort);
    void touchClient (int clientPort);
    void send (int clientPort, const juce::OSCMessage& message);
    void broadcast (const juce::OSCMessage& message);

    void sendHelloAck (int clientPort);
    void sendParamDescriptions (int clientPort);
    void sendPeaks (int clientPort, int requestId, int startFrame, int endFrame, int width);
    void sendFileList (int clientPort, int requestId, const juce::File& dir);

    /** Builds the current state as OSC messages, grouped so unchanged groups are skipped. */
    void publishState (bool forceAll, int onlyClientPort);
    void publishPlayheads();
    void publishChopPreview();

    void updateBoundsGesture (int sliceIdx, int start, int end);
    void commitBoundsGesture();

    int resolveSliceIndex (int requested) const;
    int resolveSampleId (int requested) const;
    void startAutoChopAnalysis (int sliceIdx);

    IntersectProcessor& processor;
    std::unique_ptr<juce::DatagramSocket> socket;
    juce::OSCReceiver receiver;
    juce::OSCSender sender;
    int port = 0;
    juce::String instanceId;
    juce::String trackName;
    InstanceRegistry registry;

    std::vector<Client> clients;
    SentState sent;
    uint32_t lastSnapshotVersion = 0;
    BoundsGesture boundsGesture;

    // Auto chop: one analysis at a time, for one slice's bounds.
    std::unique_ptr<OdfJob> odfJob;
    AudioAnalysis::ODFResult odf;
    int odfSliceIdx = -1;
    int odfStart = -1;
    int odfEnd = -1;
    bool odfReady = false;
    float chopSensitivity = 50.0f;   // 0..100, as in the AUTO panel
    float chopMinMs = 100.0f;
    bool chopPreviewDirty = false;
    std::vector<int> chopPositions;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RemoteControlServer)
};
