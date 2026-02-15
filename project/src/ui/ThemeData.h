#pragma once
#include <juce_graphics/juce_graphics.h>

struct ThemeData
{
    juce::String name;

    juce::Colour background;
    juce::Colour waveformBg;
    juce::Colour darkBar;
    juce::Colour foreground;
    juce::Colour tealHeader;
    juce::Colour waveformOrange;
    juce::Colour slicePink;
    juce::Colour slicePinkSel;
    juce::Colour purpleOverlay;
    juce::Colour lockGold;
    juce::Colour lockDim;
    juce::Colour gridLine;
    juce::Colour accent;
    juce::Colour button;
    juce::Colour buttonHover;
    juce::Colour separator;

    static ThemeData darkTheme()
    {
        ThemeData t;
        t.name          = "dark";
        t.background    = juce::Colour (0xFF0A0A0E);
        t.waveformBg    = juce::Colour (0xFF060608);
        t.darkBar       = juce::Colour (0xFF0E0E13);
        t.foreground    = juce::Colour (0xFFCCD0D8);
        t.tealHeader    = juce::Colour (0xFF0D0D14);
        t.waveformOrange = juce::Colour::fromFloatRGBA (0.70f, 0.78f, 0.85f, 1.0f);
        t.slicePink     = juce::Colour (0xFF2A2A35);
        t.slicePinkSel  = juce::Colour (0xFF3A3A50);
        t.purpleOverlay = juce::Colour::fromFloatRGBA (0.25f, 0.35f, 0.55f, 1.0f);
        t.lockGold      = juce::Colour::fromFloatRGBA (0.90f, 0.35f, 0.22f, 1.0f);
        t.lockDim       = juce::Colour::fromFloatRGBA (0.30f, 0.30f, 0.34f, 1.0f);
        t.gridLine      = juce::Colour::fromFloatRGBA (0.14f, 0.14f, 0.18f, 1.0f);
        t.accent        = juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f);
        t.button        = juce::Colour (0xFF1C2028);
        t.buttonHover   = juce::Colour (0xFF2A3040);
        t.separator     = juce::Colour::fromFloatRGBA (0.20f, 0.20f, 0.25f, 1.0f);
        return t;
    }

    static ThemeData lightTheme()
    {
        ThemeData t;
        t.name          = "light";
        t.background    = juce::Colour (0xFFF0F0F4);
        t.waveformBg    = juce::Colour (0xFFFAFAFE);
        t.darkBar       = juce::Colour (0xFFE8E8F0);
        t.foreground    = juce::Colour (0xFF1A1A2E);
        t.tealHeader    = juce::Colour (0xFFE0E0EC);
        t.waveformOrange = juce::Colour (0xFF2A4060);
        t.slicePink     = juce::Colour (0xFFD0D0DD);
        t.slicePinkSel  = juce::Colour (0xFFB8B8CC);
        t.purpleOverlay = juce::Colour (0xFF8090B8);
        t.lockGold      = juce::Colour (0xFFCC4422);
        t.lockDim       = juce::Colour (0xFF9999A8);
        t.gridLine      = juce::Colour (0xFFD8D8E0);
        t.accent        = juce::Colour (0xFF1A8888);
        t.button        = juce::Colour (0xFFD0D4DC);
        t.buttonHover   = juce::Colour (0xFFBCC0CC);
        t.separator     = juce::Colour (0xFFC0C0CC);
        return t;
    }

    static juce::Colour parseHex (const juce::String& hex)
    {
        return juce::Colour ((juce::uint32) (0xFF000000 | hex.getHexValue32()));
    }

    static ThemeData fromYaml (const juce::String& yaml)
    {
        ThemeData t = darkTheme(); // defaults

        for (auto line : juce::StringArray::fromLines (yaml))
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith ("#"))
                continue;

            int colonIdx = line.indexOf (":");
            if (colonIdx < 0)
                continue;

            auto key = line.substring (0, colonIdx).trim();
            auto val = line.substring (colonIdx + 1).trim().unquoted();

            if (key == "name")            t.name = val;
            else if (key == "background")    t.background = parseHex (val);
            else if (key == "waveformBg")    t.waveformBg = parseHex (val);
            else if (key == "darkBar")       t.darkBar = parseHex (val);
            else if (key == "foreground")    t.foreground = parseHex (val);
            else if (key == "tealHeader")    t.tealHeader = parseHex (val);
            else if (key == "waveformOrange") t.waveformOrange = parseHex (val);
            else if (key == "slicePink")     t.slicePink = parseHex (val);
            else if (key == "slicePinkSel")  t.slicePinkSel = parseHex (val);
            else if (key == "purpleOverlay") t.purpleOverlay = parseHex (val);
            else if (key == "lockGold")      t.lockGold = parseHex (val);
            else if (key == "lockDim")       t.lockDim = parseHex (val);
            else if (key == "gridLine")      t.gridLine = parseHex (val);
            else if (key == "accent")        t.accent = parseHex (val);
            else if (key == "button")        t.button = parseHex (val);
            else if (key == "buttonHover")   t.buttonHover = parseHex (val);
            else if (key == "separator")     t.separator = parseHex (val);
        }
        return t;
    }

    static juce::String colourToHex (juce::Colour c)
    {
        return juce::String::toHexString ((int) (c.getARGB() & 0x00FFFFFF)).paddedLeft ('0', 6);
    }

    juce::String toYaml() const
    {
        juce::String s;
        s << "name: " << name << "\n";
        s << "background: " << colourToHex (background) << "\n";
        s << "waveformBg: " << colourToHex (waveformBg) << "\n";
        s << "darkBar: " << colourToHex (darkBar) << "\n";
        s << "foreground: " << colourToHex (foreground) << "\n";
        s << "tealHeader: " << colourToHex (tealHeader) << "\n";
        s << "waveformOrange: " << colourToHex (waveformOrange) << "\n";
        s << "slicePink: " << colourToHex (slicePink) << "\n";
        s << "slicePinkSel: " << colourToHex (slicePinkSel) << "\n";
        s << "purpleOverlay: " << colourToHex (purpleOverlay) << "\n";
        s << "lockGold: " << colourToHex (lockGold) << "\n";
        s << "lockDim: " << colourToHex (lockDim) << "\n";
        s << "gridLine: " << colourToHex (gridLine) << "\n";
        s << "accent: " << colourToHex (accent) << "\n";
        s << "button: " << colourToHex (button) << "\n";
        s << "buttonHover: " << colourToHex (buttonHover) << "\n";
        s << "separator: " << colourToHex (separator) << "\n";
        return s;
    }
};
