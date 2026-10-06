#include "ParamLayout.h"
#include "../Constants.h"
#include "ParamIds.h"

juce::AudioProcessorValueTreeState::ParameterLayout ParamLayout::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // The "default*" parameters and masterVolume are LEGACY. Sample-level settings are stored
    // per session sample (SampleParams / SampleParamTable) and these are no longer read at
    // runtime. They stay registered, non-automatable, so projects saved before per-sample
    // params still load: restoreState() copies their values into every session sample.
    // Their IDs, ranges and defaults must not change.

    // ── Most-reached-for (first 8) ────────────────────────────────────────────

    // Sample BPM: 20..999, default 120
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultBpm, 1 },
        "Sample BPM (legacy)",
        juce::NormalisableRange<float> (20.0f, 999.0f, 0.01f),
        120.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Pitch: -48..+48 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultPitch, 1 },
        "Sample Pitch (legacy)",
        juce::NormalisableRange<float> (-48.0f, 48.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Cents Detune: -100..+100 cents, step 0.1, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultCentsDetune, 1 },
        "Sample Cents Detune (legacy)",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Algorithm: 0=Repitch, 1=Stretch, 2=Bungee
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultAlgorithm, 1 },
        "Sample Algorithm (legacy)",
        juce::StringArray { "Repitch", "Signalsmith", "Bungee" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // Sample Repitch Mode: 0=Linear, 1=Cubic
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultRepitchMode, 1 },
        "Sample Repitch Mode (legacy)",
        juce::StringArray { "Linear", "Cubic" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // Sample Attack: 0..1000 ms, default 5ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultAttack, 1 },
        "Sample Attack (legacy)",
        juce::NormalisableRange<float> (0.0f, 1000.0f, 0.1f),
        5.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Release: 0..5000 ms, default 20ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultRelease, 1 },
        "Sample Release (legacy)",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f),
        20.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Master Gain: -100..+24 dB, default 0 dB (unity)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::masterVolume, 1 },
        "Master Gain (legacy)",
        juce::NormalisableRange<float> (-100.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // ── Secondary sample params ────────────────────────────────────────────────

    // Sample Reverse: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultReverse, 1 },
        "Sample Reverse (legacy)",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // Sample Loop Mode: Off/Loop/Ping-Pong
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultLoop, 1 },
        "Sample Loop Mode (legacy)",
        juce::StringArray { "Off", "Loop", "Ping-Pong" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // Sample Stretch: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultStretchEnabled, 1 },
        "Sample Stretch (legacy)",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // Sample Mute Group: 0..32, default 0 (off)
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::defaultMuteGroup, 1 },
        "Sample Mute Group (legacy)",
        0, kMaxMuteGroups, 0,
        juce::AudioParameterIntAttributes().withAutomatable (false)));

    // Sample Decay: 0..5000 ms, default 100ms
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultDecay, 1 },
        "Sample Decay (legacy)",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Sustain: 0..100%, default 100%
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultSustain, 1 },
        "Sample Sustain (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Release Tail: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultReleaseTail, 1 },
        "Sample Release Tail (legacy)",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // Sample One Shot: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultOneShot, 1 },
        "Sample One Shot (legacy)",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // ── Advanced / algorithm-specific ─────────────────────────────────────────

    // Sample Tonality: 0..8000 Hz, default 0 (off)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultTonality, 1 },
        "Sample Tonality (legacy)",
        juce::NormalisableRange<float> (0.0f, 8000.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Formant: -24..+24 semitones, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFormant, 1 },
        "Sample Formant (legacy)",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // Sample Formant Comp: off/on
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultFormantComp, 1 },
        "Sample Formant Comp (legacy)",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    // Sample Grain Mode (Bungee): 0=Fast(-1), 1=Normal(0), 2=Smooth(+1)
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultGrainMode, 1 },
        "Sample Grain Mode (legacy)",
        juce::StringArray { "Fast", "Normal", "Smooth" },
        1,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));  // default = Normal

    // ── Filter ────────────────────────────────────────────────────────────────

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultFilterEnabled, 1 },
        "Filter Enabled (legacy)",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultFilterType, 1 },
        "Filter Type (legacy)",
        juce::StringArray { "LP", "HP", "BP", "NT" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultFilterSlope, 1 },
        "Filter Slope (legacy)",
        juce::StringArray { "12dB", "24dB" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    {
        auto cutoffRange = juce::NormalisableRange<float> (kMinFilterCutoffHz, kMaxFilterCutoffHz, 1.0f);
        cutoffRange.setSkewForCentre (1000.0f);
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIds::defaultFilterCutoff, 1 },
            "Filter Cutoff (legacy)",
            cutoffRange,
            8200.0f,
            juce::AudioParameterFloatAttributes().withAutomatable (false)));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterReso, 1 },
        "Filter Resonance (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterDrive, 1 },
        "Filter Drive (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterAsym, 1 },
        "Filter Drive Asymmetry (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterKeyTrack, 1 },
        "Filter Key Track (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvAttack, 1 },
        "Filter Env Attack (legacy)",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvDecay, 1 },
        "Filter Env Decay (legacy)",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvSustain, 1 },
        "Filter Env Sustain (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvRelease, 1 },
        "Filter Env Release (legacy)",
        juce::NormalisableRange<float> (0.0f, 10000.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterEnvAmount, 1 },
        "Filter Env Amount (legacy)",
        juce::NormalisableRange<float> (-96.0f, 96.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

    // ── Output ─────────────────────────────────────────────────────────────────

    // Sample Crossfade: 0..100%, default 0
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultCrossfade, 1 },
        "Sample Crossfade (legacy)",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withAutomatable (false)));

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
