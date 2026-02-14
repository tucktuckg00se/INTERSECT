#include "ParamLayout.h"
#include "ParamIds.h"

juce::AudioProcessorValueTreeState::ParameterLayout ParamLayout::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Master Volume: 0..1, default 1.0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::masterVolume, 1 },
        "Master Volume",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        1.0f));

    // Default BPM: 20..999, default 120
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultBpm, 1 },
        "Default BPM",
        juce::NormalisableRange<float> (20.0f, 999.0f, 0.1f),
        120.0f));

    // Default Pitch: -24..+24 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultPitch, 1 },
        "Default Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
        0.0f));

    // Default Algorithm: 0=Repitch, 1=Stretch
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultAlgorithm, 1 },
        "Default Algorithm",
        juce::StringArray { "Repitch", "Stretch" },
        0));

    // Default Attack: 0..1000 ms, default 5ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultAttack, 1 },
        "Default Attack",
        juce::NormalisableRange<float> (0.0f, 1000.0f, 0.1f),
        5.0f));

    // Default Decay: 0..5000 ms, default 100ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultDecay, 1 },
        "Default Decay",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f),
        100.0f));

    // Default Sustain: 0..100%, default 100%
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultSustain, 1 },
        "Default Sustain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f));

    // Default Release: 0..5000 ms, default 20ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultRelease, 1 },
        "Default Release",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f),
        20.0f));

    // Default Mute Group: 0..32, default 0 (off)
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::defaultMuteGroup, 1 },
        "Default Mute Group",
        0, 32, 0));

    // Default Ping-Pong: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultPingPong, 1 },
        "Default Ping-Pong",
        false));

    // Default Stretch Enabled: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultStretchEnabled, 1 },
        "Default Stretch Enabled",
        false));

    // Default Tonality: 0..8000 Hz, default 0 (off)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultTonality, 1 },
        "Default Tonality",
        juce::NormalisableRange<float> (0.0f, 8000.0f, 1.0f),
        0.0f));

    // Default Formant: -24..+24 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFormant, 1 },
        "Default Formant",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
        0.0f));

    // Default Formant Compensation: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultFormantComp, 1 },
        "Default Formant Compensation",
        false));

    // UI Scale: 0.5..3.0, default 1.0, step 0.25
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::uiScale, 1 },
        "UI Scale",
        juce::NormalisableRange<float> (0.5f, 3.0f, 0.25f),
        1.0f));

    return { params.begin(), params.end() };
}
