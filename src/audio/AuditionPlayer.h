#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

/** A file decoded for audition: stereo audio at the host rate plus peaks for the preview waveform. */
struct AuditionClip
{
    static constexpr int kPeakBuckets = 1024;

    juce::File file;
    juce::AudioBuffer<float> audio;   // always stereo
    double sampleRate = 0.0;
    std::vector<float> peaks;         // per-bucket absolute peak, 0..1
    bool truncated = false;           // only the first maxSeconds were decoded
    double previewSeconds = 0.0;      // length of the decoded part

    /** Decodes up to maxSeconds from the start of the file on the calling (background) thread.
        Longer files are truncated so memory stays bounded. Returns nullptr on failure or abort. */
    static std::shared_ptr<const AuditionClip> decode (const juce::File& file,
                                                       double targetSampleRate,
                                                       double maxSeconds,
                                                       const std::function<bool()>& shouldAbort);
};

/** What the browser shows about audition. Built on the message thread. */
struct AuditionStatus
{
    enum class State
    {
        idle,
        decoding,
        playing,
        paused,
        stopped,
        failed,
    };

    State state = State::idle;
    juce::File file;
    float position = 0.0f;                       // 0..1 of the clip
    std::shared_ptr<const AuditionClip> clip;    // last decoded clip, may be for another file
};

/**
    Real-time-safe playback of one AuditionClip, mixed into the main output.

    The message thread owns every clip through shared_ptrs. The audio thread only sees a raw
    pointer published through an atomic with a generation number. A replaced clip is parked in
    a retired list and freed on the message thread once the audio thread has switched away from
    it (it acknowledged the newest generation, or it is not inside renderAdd). The audio thread
    never allocates or frees.
*/
class AuditionPlayer
{
public:
    // ---- Message thread ----
    /** Starts clip from the beginning (the clip may be the one already loaded). */
    void play (std::shared_ptr<const AuditionClip> clip);
    /** Fades out and holds the position, so resume() continues from there. */
    void pause();
    /** Continues a paused clip; a clip that played to the end starts again. */
    void resume();
    /** Fades out and rewinds. The clip stays loaded for display and replay. */
    void stop();
    void setGainDb (float db) noexcept;
    std::shared_ptr<const AuditionClip> getClip() const { return current; }
    bool isPlaying() const noexcept { return playing.load(); }
    /** 0..1 through the clip; held while paused, 0 after stop(), 1 once it played to the end. */
    float getPosition() const noexcept;
    /** Frees clips the audio thread can no longer reach. Also called by the transport calls. */
    void releaseRetiredClips();

    // ---- Audio thread ----
    /** Adds audition audio into left/right (either may be null). Returns true if it produced audio. */
    bool renderAdd (float* left, float* right, int numSamples) noexcept;
    /** PANIC: stop immediately. */
    void stopFromAudioThread() noexcept { panicRequested = true; }

private:
    static constexpr int kFadeSamples = 256;

    // Transport commands. Only the most recent one matters, so a command is a value plus a
    // sequence number; the audio thread applies the latest value whenever the sequence moves.
    enum Command
    {
        commandNone = 0,
        commandPause,
        commandResume,
        commandStop,
    };
    void sendCommand (Command command);

    // Message thread only.
    std::shared_ptr<const AuditionClip> current;
    std::vector<std::shared_ptr<const AuditionClip>> retired;

    // Shared between threads (sequentially consistent on purpose; see releaseRetiredClips).
    std::atomic<const AuditionClip*> published { nullptr };
    std::atomic<uint32_t> publishedGeneration { 0 };
    std::atomic<uint32_t> acknowledgedGeneration { 0 };
    std::atomic<int> latestCommand { commandNone };
    std::atomic<uint32_t> commandSequence { 0 };
    std::atomic<uint32_t> commandSequenceAtPublish { 0 };
    std::atomic<bool> audioThreadRendering { false };
    std::atomic<bool> playing { false };
    std::atomic<float> targetGain { 0.5f };
    std::atomic<int> positionFrames { 0 };
    std::atomic<int> lengthFrames { 0 };

    // Audio thread only.
    const AuditionClip* active = nullptr;
    uint32_t activeGeneration = 0;
    uint32_t seenCommandSequence = 0;
    bool rewindAfterFade = false;
    int readPos = 0;
    int fadeInRemaining = 0;
    int fadeOutRemaining = 0;
    float currentGain = 0.5f;
    bool activePlaying = false;
    bool panicRequested = false;
};
