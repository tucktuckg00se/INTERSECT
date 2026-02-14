#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Dark warehouse techno palette
namespace Theme
{
    // Core backgrounds — near-black with cold undertones
    inline const juce::Colour background     { 0xFF0A0A0E };
    inline const juce::Colour waveformBg     { 0xFF060608 };
    inline const juce::Colour darkBar        { 0xFF0E0E13 };

    // Text
    inline const juce::Colour foreground     { 0xFFCCD0D8 };

    // Header — very dark, minimal separation
    inline const juce::Colour tealHeader     { 0xFF0D0D14 };

    // Waveform — cold oscilloscope white
    inline const juce::Colour waveformOrange = juce::Colour::fromFloatRGBA (0.70f, 0.78f, 0.85f, 1.0f);

    // Slices — muted industrial
    inline const juce::Colour slicePink      { 0xFF2A2A35 };
    inline const juce::Colour slicePinkSel   { 0xFF3A3A50 };
    inline const juce::Colour purpleOverlay  = juce::Colour::fromFloatRGBA (0.25f, 0.35f, 0.55f, 1.0f);

    // Lock state — amber warn / dim
    inline const juce::Colour lockGold       = juce::Colour::fromFloatRGBA (0.90f, 0.35f, 0.22f, 1.0f);
    inline const juce::Colour lockDim        = juce::Colour::fromFloatRGBA (0.30f, 0.30f, 0.34f, 1.0f);

    // Grid — barely visible
    inline const juce::Colour gridLine       = juce::Colour::fromFloatRGBA (0.14f, 0.14f, 0.18f, 1.0f);

    // Accent — cold cyan
    inline const juce::Colour accent         = juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f);

    // Buttons — dark industrial charcoal
    inline const juce::Colour button         { 0xFF1C2028 };
    inline const juce::Colour buttonHover    { 0xFF2A3040 };

    // Separator
    inline const juce::Colour separator      = juce::Colour::fromFloatRGBA (0.20f, 0.20f, 0.25f, 1.0f);
}

class IntersectLookAndFeel : public juce::LookAndFeel_V4
{
public:
    IntersectLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
};
