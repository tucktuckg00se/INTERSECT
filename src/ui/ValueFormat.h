#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

/** How the signal-chain bar writes values ("12.84k", "+0st", "41%"). Header-only. */
namespace ValueFormat
{
inline juce::String formatTrimmed (float value, int decimals)
{
    juce::String text (value, decimals);
    if (text.containsChar ('.'))
    {
        while (text.endsWithChar ('0'))
            text = text.dropLastCharacters (1);
        if (text.endsWithChar ('.'))
            text = text.dropLastCharacters (1);
    }
    return text;
}

inline juce::String formatSigned (float value, int decimals, const juce::String& suffix)
{
    return (value >= 0.0f ? "+" : "") + formatTrimmed (value, decimals) + suffix;
}

inline juce::String formatBool (bool on) { return on ? "ON" : "OFF"; }
inline juce::String formatMs (float valueMs) { return juce::String (juce::roundToInt (valueMs)) + "ms"; }
inline juce::String formatPercent (float valuePercent, int decimals = 0) { return formatTrimmed (valuePercent, decimals) + "%"; }
inline juce::String formatGain (float valueDb) { return (valueDb >= 0.0f ? "+" : "") + formatTrimmed (valueDb, 1) + "dB"; }

inline juce::String formatHz (float valueHz)
{
    if (valueHz >= 1000.0f)
        return formatTrimmed (valueHz / 1000.0f, 2) + "k";
    return formatTrimmed (valueHz, 0);
}

inline juce::String getChoiceName (int value, const juce::StringArray& names)
{
    return names[juce::jlimit (0, names.size() - 1, value)];
}

inline juce::String midiNoteName (int note, int middleCOctave)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, middleCOctave);
}
}
