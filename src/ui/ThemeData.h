#pragma once
#include <juce_graphics/juce_graphics.h>

struct ThemeData
{
    juce::String name;

    juce::Colour background;
    juce::Colour waveformBg;
    juce::Colour darkBar;
    juce::Colour foreground;
    juce::Colour header;
    juce::Colour waveform;
    juce::Colour selectionOverlay;
    juce::Colour lockActive;
    juce::Colour lockInactive;
    juce::Colour gridLine;
    juce::Colour accent;
    juce::Colour button;
    juce::Colour buttonHover;
    juce::Colour separator;

    juce::Colour slicePalette[16];

    static ThemeData darkTheme()
    {
        ThemeData t;
        t.name          = "dark";
        t.background    = juce::Colour (0xFF0A0A0E);
        t.waveformBg    = juce::Colour (0xFF060608);
        t.darkBar       = juce::Colour (0xFF0E0E13);
        t.foreground    = juce::Colour (0xFFCCD0D8);
        t.header    = juce::Colour (0xFF0D0D14);
        t.waveform = juce::Colour::fromFloatRGBA (0.70f, 0.78f, 0.85f, 1.0f);
        t.selectionOverlay = juce::Colour::fromFloatRGBA (0.25f, 0.35f, 0.55f, 1.0f);
        t.lockActive      = juce::Colour::fromFloatRGBA (0.90f, 0.35f, 0.22f, 1.0f);
        t.lockInactive       = juce::Colour::fromFloatRGBA (0.30f, 0.30f, 0.34f, 1.0f);
        t.gridLine      = juce::Colour::fromFloatRGBA (0.14f, 0.14f, 0.18f, 1.0f);
        t.accent        = juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f);
        t.button        = juce::Colour (0xFF1C2028);
        t.buttonHover   = juce::Colour (0xFF2A3040);
        t.separator     = juce::Colour::fromFloatRGBA (0.20f, 0.20f, 0.25f, 1.0f);
        t.slicePalette[0]  = juce::Colour (0xFF4D8C99); // Cold Teal
        t.slicePalette[1]  = juce::Colour (0xFF8C4747); // Muted Red
        t.slicePalette[2]  = juce::Colour (0xFF4D8059); // Dark Green
        t.slicePalette[3]  = juce::Colour (0xFF8C7340); // Rust
        t.slicePalette[4]  = juce::Colour (0xFF664D8C); // Dusk Violet
        t.slicePalette[5]  = juce::Colour (0xFF80804D); // Olive
        t.slicePalette[6]  = juce::Colour (0xFF40808C); // Steel Cyan
        t.slicePalette[7]  = juce::Colour (0xFF804D6B); // Dark Rose
        t.slicePalette[8]  = juce::Colour (0xFF597A47); // Moss
        t.slicePalette[9]  = juce::Colour (0xFF80594D); // Clay
        t.slicePalette[10] = juce::Colour (0xFF52598C); // Slate Blue
        t.slicePalette[11] = juce::Colour (0xFF737359); // Concrete
        t.slicePalette[12] = juce::Colour (0xFF6B4773); // Plum
        t.slicePalette[13] = juce::Colour (0xFF477A6B); // Patina
        t.slicePalette[14] = juce::Colour (0xFF7A5973); // Mauve
        t.slicePalette[15] = juce::Colour (0xFF617A66); // Lichen
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
        t.header    = juce::Colour (0xFFE0E0EC);
        t.waveform = juce::Colour (0xFF2A4060);
        t.selectionOverlay = juce::Colour (0xFF8090B8);
        t.lockActive      = juce::Colour (0xFFCC4422);
        t.lockInactive       = juce::Colour (0xFF9999A8);
        t.gridLine      = juce::Colour (0xFFD8D8E0);
        t.accent        = juce::Colour (0xFF1A8888);
        t.button        = juce::Colour (0xFFD0D4DC);
        t.buttonHover   = juce::Colour (0xFFBCC0CC);
        t.separator     = juce::Colour (0xFFC0C0CC);
        t.slicePalette[0]  = juce::Colour (0xFF5AABB8); // Cold Teal
        t.slicePalette[1]  = juce::Colour (0xFFB85A5A); // Muted Red
        t.slicePalette[2]  = juce::Colour (0xFF5AA66E); // Dark Green
        t.slicePalette[3]  = juce::Colour (0xFFB89650); // Rust
        t.slicePalette[4]  = juce::Colour (0xFF8066B8); // Dusk Violet
        t.slicePalette[5]  = juce::Colour (0xFFA6A65A); // Olive
        t.slicePalette[6]  = juce::Colour (0xFF50A6B8); // Steel Cyan
        t.slicePalette[7]  = juce::Colour (0xFFB8668E); // Dark Rose
        t.slicePalette[8]  = juce::Colour (0xFF6E9E5A); // Moss
        t.slicePalette[9]  = juce::Colour (0xFFB87A66); // Clay
        t.slicePalette[10] = juce::Colour (0xFF6670B8); // Slate Blue
        t.slicePalette[11] = juce::Colour (0xFF96966E); // Concrete
        t.slicePalette[12] = juce::Colour (0xFF8E5A98); // Plum
        t.slicePalette[13] = juce::Colour (0xFF5A9E88); // Patina
        t.slicePalette[14] = juce::Colour (0xFFA07098); // Mauve
        t.slicePalette[15] = juce::Colour (0xFF7A9E80); // Lichen
        return t;
    }

    static juce::Colour parseHex (const juce::String& hex)
    {
        return juce::Colour ((juce::uint32) (0xFF000000 | hex.getHexValue32()));
    }

    static ThemeData fromThemeFile (const juce::String& text)
    {
        ThemeData t = darkTheme(); // defaults

        for (auto line : juce::StringArray::fromLines (text))
        {
            line = line.trim();
            if (line.isEmpty() || line.startsWith ("#"))
                continue;

            int colonIdx = line.indexOf (":");
            if (colonIdx < 0)
                continue;

            auto key = line.substring (0, colonIdx).trim();
            auto val = line.substring (colonIdx + 1).trim().unquoted();

            // Strip inline comments (  # ...)
            int hashIdx = val.indexOf (" #");
            if (hashIdx >= 0)
                val = val.substring (0, hashIdx).trimEnd();

            if (key == "name")            t.name = val;
            else if (key == "background")    t.background = parseHex (val);
            else if (key == "waveformBg")    t.waveformBg = parseHex (val);
            else if (key == "darkBar")       t.darkBar = parseHex (val);
            else if (key == "foreground")    t.foreground = parseHex (val);
            else if (key == "header")    t.header = parseHex (val);
            else if (key == "waveform") t.waveform = parseHex (val);
            else if (key == "selectionOverlay") t.selectionOverlay = parseHex (val);
            else if (key == "lockActive")      t.lockActive = parseHex (val);
            else if (key == "lockInactive")       t.lockInactive = parseHex (val);
            else if (key == "gridLine")      t.gridLine = parseHex (val);
            else if (key == "accent")        t.accent = parseHex (val);
            else if (key == "button")        t.button = parseHex (val);
            else if (key == "buttonHover")   t.buttonHover = parseHex (val);
            else if (key == "separator")     t.separator = parseHex (val);
            else if (key.startsWith ("slice"))
            {
                int idx = key.substring (5).getIntValue() - 1;
                if (idx >= 0 && idx < 16)
                    t.slicePalette[idx] = parseHex (val);
            }
        }
        return t;
    }

    static juce::String colourToHex (juce::Colour c)
    {
        return juce::String::toHexString ((int) (c.getARGB() & 0x00FFFFFF)).paddedLeft ('0', 6);
    }

    juce::String toThemeFile() const
    {
        juce::String s;
        s << "name: " << name << "\n";
        s << "background: " << colourToHex (background) << "\n";
        s << "waveformBg: " << colourToHex (waveformBg) << "\n";
        s << "darkBar: " << colourToHex (darkBar) << "\n";
        s << "foreground: " << colourToHex (foreground) << "\n";
        s << "header: " << colourToHex (header) << "\n";
        s << "waveform: " << colourToHex (waveform) << "\n";
        s << "selectionOverlay: " << colourToHex (selectionOverlay) << "\n";
        s << "lockActive: " << colourToHex (lockActive) << "\n";
        s << "lockInactive: " << colourToHex (lockInactive) << "\n";
        s << "gridLine: " << colourToHex (gridLine) << "\n";
        s << "accent: " << colourToHex (accent) << "\n";
        s << "button: " << colourToHex (button) << "\n";
        s << "buttonHover: " << colourToHex (buttonHover) << "\n";
        s << "separator: " << colourToHex (separator) << "\n";
        for (int i = 0; i < 16; ++i)
            s << "slice" << (i + 1) << ": " << colourToHex (slicePalette[i]) << "\n";
        return s;
    }
};
