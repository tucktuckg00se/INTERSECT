#pragma once
#include <juce_graphics/juce_graphics.h>

struct ThemeData
{
    juce::String name;

    // 6 surface levels (darkest → lightest in dark themes, reversed in light)
    juce::Colour surface0, surface1, surface2, surface3, surface4, surface5;

    // 3 text levels (muted → primary)
    juce::Colour text0, text1, text2;

    // Waveform + accent
    juce::Colour waveform;
    juce::Colour accent;

    // 5 semantic colors
    juce::Colour color1, color2, color3, color4, color5;

    // Slice palette (unchanged)
    juce::Colour slicePalette[16];

    static ThemeData darkTheme()
    {
        ThemeData t;
        t.name     = "dark";
        t.surface0 = juce::Colour (0xFF060608);
        t.surface1 = juce::Colour (0xFF0a0a0e);
        t.surface2 = juce::Colour (0xFF0e0e13);
        t.surface3 = juce::Colour (0xFF181c24);
        t.surface4 = juce::Colour (0xFF23232d);
        t.surface5 = juce::Colour (0xFF2a3040);
        t.text0    = juce::Colour (0xFF506070);
        t.text1    = juce::Colour (0xFF7888a0);
        t.text2    = juce::Colour (0xFFccd0d8);
        t.waveform = juce::Colour (0xFFb2c6d8);
        t.accent   = juce::Colour (0xFF3fd8d8);
        t.color1   = juce::Colour (0xFF4a7098);
        t.color2   = juce::Colour (0xFF98783a);
        t.color3   = juce::Colour (0xFF4a7858);
        t.color4   = juce::Colour (0xFF685090);
        t.color5   = juce::Colour (0xFFcc4444);
        t.slicePalette[0]  = juce::Colour (0xFF4d8c99);
        t.slicePalette[1]  = juce::Colour (0xFF8c4747);
        t.slicePalette[2]  = juce::Colour (0xFF4d8059);
        t.slicePalette[3]  = juce::Colour (0xFF8c7340);
        t.slicePalette[4]  = juce::Colour (0xFF664d8c);
        t.slicePalette[5]  = juce::Colour (0xFF80804d);
        t.slicePalette[6]  = juce::Colour (0xFF40808c);
        t.slicePalette[7]  = juce::Colour (0xFF804d6b);
        t.slicePalette[8]  = juce::Colour (0xFF597a47);
        t.slicePalette[9]  = juce::Colour (0xFF80594d);
        t.slicePalette[10] = juce::Colour (0xFF52598c);
        t.slicePalette[11] = juce::Colour (0xFF737359);
        t.slicePalette[12] = juce::Colour (0xFF6b4773);
        t.slicePalette[13] = juce::Colour (0xFF477a6b);
        t.slicePalette[14] = juce::Colour (0xFF7a5973);
        t.slicePalette[15] = juce::Colour (0xFF617a66);
        return t;
    }

    static ThemeData lightTheme()
    {
        ThemeData t;
        t.name     = "light";
        t.surface0 = juce::Colour (0xFFfafafe);
        t.surface1 = juce::Colour (0xFFf0f0f4);
        t.surface2 = juce::Colour (0xFFe8e8f0);
        t.surface3 = juce::Colour (0xFFd0d4dc);
        t.surface4 = juce::Colour (0xFFbcc0cc);
        t.surface5 = juce::Colour (0xFF9090a0);
        t.text0    = juce::Colour (0xFF9090a0);
        t.text1    = juce::Colour (0xFF5e6874);
        t.text2    = juce::Colour (0xFF1a1a2e);
        t.waveform = juce::Colour (0xFF2a4060);
        t.accent   = juce::Colour (0xFF1a8888);
        t.color1   = juce::Colour (0xFF4a7098);
        t.color2   = juce::Colour (0xFF98783a);
        t.color3   = juce::Colour (0xFF4a7858);
        t.color4   = juce::Colour (0xFF685090);
        t.color5   = juce::Colour (0xFFcc4444);
        t.slicePalette[0]  = juce::Colour (0xFF5aabb8);
        t.slicePalette[1]  = juce::Colour (0xFFb85a5a);
        t.slicePalette[2]  = juce::Colour (0xFF5aa66e);
        t.slicePalette[3]  = juce::Colour (0xFFb89650);
        t.slicePalette[4]  = juce::Colour (0xFF8066b8);
        t.slicePalette[5]  = juce::Colour (0xFFa6a65a);
        t.slicePalette[6]  = juce::Colour (0xFF50a6b8);
        t.slicePalette[7]  = juce::Colour (0xFFb8668e);
        t.slicePalette[8]  = juce::Colour (0xFF6e9e5a);
        t.slicePalette[9]  = juce::Colour (0xFFb87a66);
        t.slicePalette[10] = juce::Colour (0xFF6670b8);
        t.slicePalette[11] = juce::Colour (0xFF96966e);
        t.slicePalette[12] = juce::Colour (0xFF8e5a98);
        t.slicePalette[13] = juce::Colour (0xFF5a9e88);
        t.slicePalette[14] = juce::Colour (0xFFa07098);
        t.slicePalette[15] = juce::Colour (0xFF7a9e80);
        return t;
    }

    static juce::Colour parseHex (const juce::String& hex)
    {
        const auto rgb = static_cast<juce::uint32> (hex.getHexValue32()) & 0x00ffffffu;
        return juce::Colour (0xFF000000u | rgb);
    }

    static ThemeData fromThemeFile (const juce::String& text)
    {
        ThemeData t = darkTheme(); // defaults

        // Track which new-format keys were explicitly set so old-format keys
        // don't override them when both are present.
        bool hasSurface0 = false, hasSurface1 = false, hasSurface2 = false;
        bool hasSurface3 = false, hasSurface4 = false, hasSurface5 = false;
        bool hasText0 = false, hasText1 = false, hasText2 = false;
        bool hasAccent = false;
        bool hasColor1 = false, hasColor2 = false, hasColor3 = false;
        bool hasColor4 = false, hasColor5 = false;

        // First pass: read all keys
        struct KeyVal { juce::String key; juce::String val; };
        std::vector<KeyVal> entries;

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

            entries.push_back ({ key, val });
        }

        // Pass 1: Apply new-format keys first
        for (const auto& e : entries)
        {
            const auto& key = e.key;
            const auto& val = e.val;

            if (key == "name")       t.name = val;
            else if (key == "surface0") { t.surface0 = parseHex (val); hasSurface0 = true; }
            else if (key == "surface1") { t.surface1 = parseHex (val); hasSurface1 = true; }
            else if (key == "surface2") { t.surface2 = parseHex (val); hasSurface2 = true; }
            else if (key == "surface3") { t.surface3 = parseHex (val); hasSurface3 = true; }
            else if (key == "surface4") { t.surface4 = parseHex (val); hasSurface4 = true; }
            else if (key == "surface5") { t.surface5 = parseHex (val); hasSurface5 = true; }
            else if (key == "text0")    { t.text0 = parseHex (val); hasText0 = true; }
            else if (key == "text1")    { t.text1 = parseHex (val); hasText1 = true; }
            else if (key == "text2")    { t.text2 = parseHex (val); hasText2 = true; }
            else if (key == "waveform") { t.waveform = parseHex (val); }
            else if (key == "accent")   { t.accent = parseHex (val); hasAccent = true; }
            else if (key == "color1")   { t.color1 = parseHex (val); hasColor1 = true; }
            else if (key == "color2")   { t.color2 = parseHex (val); hasColor2 = true; }
            else if (key == "color3")   { t.color3 = parseHex (val); hasColor3 = true; }
            else if (key == "color4")   { t.color4 = parseHex (val); hasColor4 = true; }
            else if (key == "color5")   { t.color5 = parseHex (val); hasColor5 = true; }
            else if (key.startsWith ("slice"))
            {
                int idx = key.substring (5).getIntValue() - 1;
                if (idx >= 0 && idx < 16)
                    t.slicePalette[idx] = parseHex (val);
            }
        }

        // Pass 2: Apply old-format keys only where new-format keys were not set.
        // This provides backward compatibility for user-created themes.
        for (const auto& e : entries)
        {
            const auto& key = e.key;
            const auto& val = e.val;

            if (key == "waveformBg" && ! hasSurface0)        t.surface0 = parseHex (val);
            else if (key == "background" && ! hasSurface1)   t.surface1 = parseHex (val);
            else if ((key == "darkBar" || key == "header") && ! hasSurface2) t.surface2 = parseHex (val);
            else if ((key == "module_border" || key == "param_value_off" || key == "set_bpm_border") && ! hasSurface3)
                t.surface3 = parseHex (val);
            else if ((key == "gridLine" || key == "button") && ! hasSurface4) t.surface4 = parseHex (val);
            else if ((key == "separator" || key == "buttonHover") && ! hasSurface5) t.surface5 = parseHex (val);
            else if ((key == "lockInactive" || key == "param_label" || key == "context_text"
                      || key == "context_dim_text" || key == "tab_inactive") && ! hasText0)
                t.text0 = parseHex (val);
            else if ((key == "param_value" || key == "override_value") && ! hasText1) t.text1 = parseHex (val);
            else if (key == "foreground" && ! hasText2) t.text2 = parseHex (val);
            else if ((key == "selectionOverlay" || key == "set_bpm_text" || key == "param_value_on"
                      || key == "tab_global_active" || key == "tab_slice_active") && ! hasAccent)
                { /* accent already handled by "accent" key or new-format key */ }
            else if (key == "module_name_playback" && ! hasColor1) t.color1 = parseHex (val);
            else if ((key == "module_name_filter" || key == "filter_toggle_on") && ! hasColor2) t.color2 = parseHex (val);
            else if (key == "module_name_amp" && ! hasColor3) t.color3 = parseHex (val);
            else if (key == "module_name_output" && ! hasColor4) t.color4 = parseHex (val);
            else if ((key == "lockActive" || key == "override_bar" || key == "override_bar_hover"
                      || key == "override_label" || key == "override_count"
                      || key == "lazy_chop_overlay" || key == "preview_cursor") && ! hasColor5)
                t.color5 = parseHex (val);
            // Keys that map to existing new-format names (waveform, accent) are
            // already handled in pass 1. Old keys like context_bar_bg,
            // signal_chain_bg map to surface1 but are lower priority — skip if
            // surface1 was explicitly set.
            else if ((key == "context_bar_bg" || key == "signal_chain_bg") && ! hasSurface1)
                t.surface1 = parseHex (val);
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
        s << "\n";
        s << "surface0: " << colourToHex (surface0) << "\n";
        s << "surface1: " << colourToHex (surface1) << "\n";
        s << "surface2: " << colourToHex (surface2) << "\n";
        s << "surface3: " << colourToHex (surface3) << "\n";
        s << "surface4: " << colourToHex (surface4) << "\n";
        s << "surface5: " << colourToHex (surface5) << "\n";
        s << "\n";
        s << "text0: " << colourToHex (text0) << "\n";
        s << "text1: " << colourToHex (text1) << "\n";
        s << "text2: " << colourToHex (text2) << "\n";
        s << "\n";
        s << "waveform: " << colourToHex (waveform) << "\n";
        s << "accent: " << colourToHex (accent) << "\n";
        s << "\n";
        s << "color1: " << colourToHex (color1) << "\n";
        s << "color2: " << colourToHex (color2) << "\n";
        s << "color3: " << colourToHex (color3) << "\n";
        s << "color4: " << colourToHex (color4) << "\n";
        s << "color5: " << colourToHex (color5) << "\n";
        s << "\n";
        for (int i = 0; i < 16; ++i)
            s << "slice" << (i + 1) << ": " << colourToHex (slicePalette[i]) << "\n";
        return s;
    }
};
