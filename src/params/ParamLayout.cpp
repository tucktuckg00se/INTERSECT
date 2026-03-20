#include "ParamLayout.h"
#include "../Constants.h"
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

    // Sample Pitch: -48..+48 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultPitch, 1 },
        "Sample Pitch",
        juce::NormalisableRange<float> (-48.0f, 48.0f, 0.01f),
        0.0f));

    // Sample Cents Detune: -100..+100 cents, step 0.1, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultCentsDetune, 1 },
        "Sample Cents Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
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

    // Sample One Shot: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultOneShot, 1 },
        "Sample One Shot",
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

    // ── Filter ────────────────────────────────────────────────────────────────

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultFilterEnabled, 1 },
        "Filter Enabled",
        false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultFilterType, 1 },
        "Filter Type",
        juce::StringArray { "LP", "HP", "BP", "NT" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultFilterSlope, 1 },
        "Filter Slope",
        juce::StringArray { "12dB", "24dB" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterCutoff, 1 },
        "Filter Cutoff",
        juce::NormalisableRange<float> (kMinFilterCutoffHz, kMaxFilterCutoffHz, 1.0f),
        8200.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterReso, 1 },
        "Filter Resonance",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterDrive, 1 },
        "Filter Drive",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterKeyTrack, 1 },
        "Filter Key Track",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvAttack, 1 },
        "Filter Env Attack",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvDecay, 1 },
        "Filter Env Decay",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvSustain, 1 },
        "Filter Env Sustain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvRelease, 1 },
        "Filter Env Release",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvAmount, 1 },
        "Filter Env Amount",
        juce::NormalisableRange<float> (-96.0f, 96.0f, 0.1f),
        0.0f));

    // ── Global utility ─────────────────────────────────────────────────────────

    // Max Voices: 1..31 playable voices, preview voice is reserved
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::maxVoices, 1 },
        "Max Voices",
        1, 31, 16));

    // UI Scale: 0.5..3.0, default 1.0, step 0.25
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::uiScale, 1 },
        "UI Scale",
        juce::NormalisableRange<float> (0.5f, 3.0f, 0.25f),
        1.0f));

    return { params.begin(), params.end() };
}
