#include "ParamLayout.h"
#include "ParamIds.h"

juce::AudioProcessorValueTreeState::ParameterLayout ParamLayout::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ── Most-reached-for (first 8) ────────────────────────────────────────────

    // Sample BPM: 20..999, default 120
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultBpm, 1 },
        "Sample BPM",
        juce::NormalisableRange<float> (20.0f, 999.0f, 0.01f),
        120.0f));

    // Sample Pitch: -24..+24 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultPitch, 1 },
        "Sample Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
        0.0f));

    // Sample Algorithm: 0=Repitch, 1=Stretch, 2=Bungee
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultAlgorithm, 1 },
        "Sample Algorithm",
        juce::StringArray { "Repitch", "Stretch", "Bungee" },
        0));

    // Sample Attack: 0..1000 ms, default 5ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultAttack, 1 },
        "Sample Attack",
        juce::NormalisableRange<float> (0.0f, 1000.0f, 0.1f),
        5.0f));

    // Sample Release: 0..5000 ms, default 20ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultRelease, 1 },
        "Sample Release",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f),
        20.0f));

    // Master Gain: -100..+24 dB, default 0 dB (unity)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::masterVolume, 1 },
        "Master Gain",
        juce::NormalisableRange<float> (-100.0f, 24.0f, 0.1f),
        0.0f));

    // ── Secondary sample params ────────────────────────────────────────────────

    // Sample Reverse: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultReverse, 1 },
        "Sample Reverse",
        false));

    // Sample Loop Mode: Off/Loop/Ping-Pong
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultLoop, 1 },
        "Sample Loop Mode",
        juce::StringArray { "Off", "Loop", "Ping-Pong" },
        0));

    // Sample Stretch: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultStretchEnabled, 1 },
        "Sample Stretch",
        false));

    // Sample Mute Group: 0..32, default 0 (off)
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::defaultMuteGroup, 1 },
        "Sample Mute Group",
        0, 32, 0));

    // Sample Decay: 0..5000 ms, default 100ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultDecay, 1 },
        "Sample Decay",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f),
        100.0f));

    // Sample Sustain: 0..100%, default 100%
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultSustain, 1 },
        "Sample Sustain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f));

    // Sample Release Tail: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultReleaseTail, 1 },
        "Sample Release Tail",
        false));

    // ── Advanced / algorithm-specific ─────────────────────────────────────────

    // Sample Tonality: 0..8000 Hz, default 0 (off)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultTonality, 1 },
        "Sample Tonality",
        juce::NormalisableRange<float> (0.0f, 8000.0f, 1.0f),
        0.0f));

    // Sample Formant: -24..+24 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFormant, 1 },
        "Sample Formant",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
        0.0f));

    // Sample Formant Comp: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultFormantComp, 1 },
        "Sample Formant Comp",
        false));

    // Sample Grain Mode (Bungee): 0=Fast(-1), 1=Normal(0), 2=Smooth(+1)
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultGrainMode, 1 },
        "Sample Grain Mode",
        juce::StringArray { "Fast", "Normal", "Smooth" },
        1));  // default = Normal

    // ── Global utility ─────────────────────────────────────────────────────────

    // Max Voices: 1..32, default 16
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::maxVoices, 1 },
        "Max Voices",
        1, 32, 16));

    // UI Scale: 0.5..3.0, default 1.0, step 0.25
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::uiScale, 1 },
        "UI Scale",
        juce::NormalisableRange<float> (0.5f, 3.0f, 0.25f),
        1.0f));

    return { params.begin(), params.end() };
}
