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
    juce::Colour moduleNamePlayback;
    juce::Colour moduleNameFilter;
    juce::Colour moduleNameAmp;
    juce::Colour moduleNameOutput;
    juce::Colour tabGlobalActive;
    juce::Colour tabSliceActive;
    juce::Colour overrideBar;
    juce::Colour overrideBarHover;
    juce::Colour overrideLabel;
    juce::Colour overrideValue;
    juce::Colour overrideCount;
    juce::Colour filterToggleOn;
    juce::Colour setBpmText;
    juce::Colour setBpmBorder;
    juce::Colour paramLabel;
    juce::Colour paramValue;
    juce::Colour paramValueOn;
    juce::Colour paramValueOff;
    juce::Colour contextBarBg;
    juce::Colour signalChainBg;
    juce::Colour moduleBorder;
    juce::Colour contextText;
    juce::Colour contextDimText;
    juce::Colour tabInactive;
    juce::Colour lazyChopOverlay;
    juce::Colour previewCursor;

    juce::Colour slicePalette[16];

    static ThemeData darkTheme()
    {
        ThemeData t;
        t.name          = "dark";
        t.background    = juce::Colour (0xFF08090C);
        t.waveformBg    = juce::Colour (0xFF060710);
        t.darkBar       = juce::Colour (0xFF0C0E12);
        t.foreground    = juce::Colour (0xFF8090A0);
        t.header        = juce::Colour (0xFF0C0E12);
        t.waveform      = juce::Colour (0xFF6A7D90);
        t.selectionOverlay = juce::Colour (0xFF486888);
        t.lockActive    = juce::Colour (0xFFB84830);
        t.lockInactive  = juce::Colour (0xFF2A3038);
        t.gridLine      = juce::Colour (0xFF151A22);
        t.accent        = juce::Colour (0xFF48C0A8);
        t.button        = juce::Colour (0xFF0E1218);
        t.buttonHover   = juce::Colour (0xFF101420);
        t.separator     = juce::Colour (0xFF161A20);
        t.moduleNamePlayback = juce::Colour (0xFF4A7098);
        t.moduleNameFilter   = juce::Colour (0xFF98783A);
        t.moduleNameAmp      = juce::Colour (0xFF4A7858);
        t.moduleNameOutput   = juce::Colour (0xFF685090);
        t.tabGlobalActive = juce::Colour (0xFF4878A0);
        t.tabSliceActive  = juce::Colour (0xFFA880C8);
        t.overrideBar     = juce::Colour (0xFFB84830);
        t.overrideBarHover = juce::Colour (0xFFD86050);
        t.overrideLabel   = juce::Colour (0xFFA04030);
        t.overrideValue   = juce::Colour (0xFFC0C8D0);
        t.overrideCount   = juce::Colour (0xFF904030);
        t.filterToggleOn  = juce::Colour (0xFF806838);
        t.setBpmText      = juce::Colour (0xFF48C0A8);
        t.setBpmBorder    = juce::Colour (0xFF183028);
        t.paramLabel      = juce::Colour (0xFF384048);
        t.paramValue      = juce::Colour (0xFF7888A0);
        t.paramValueOn    = juce::Colour (0xFF48C0A8);
        t.paramValueOff   = juce::Colour (0xFF181C24);
        t.contextBarBg    = juce::Colour (0xFF0A0B10);
        t.signalChainBg   = juce::Colour (0xFF0A0C10);
        t.moduleBorder    = juce::Colour (0xFF161A20);
        t.contextText     = juce::Colour (0xFF586070);
        t.contextDimText  = juce::Colour (0xFF404858);
        t.tabInactive     = juce::Colour (0xFF384050);
        t.lazyChopOverlay = juce::Colour (0xFFCC4444);
        t.previewCursor   = juce::Colour (0xFFCC4444);
        t.slicePalette[0]  = juce::Colour (0xFF4888B8);
        t.slicePalette[1]  = juce::Colour (0xFFB85878);
        t.slicePalette[2]  = juce::Colour (0xFF48A060);
        t.slicePalette[3]  = juce::Colour (0xFF98884A);
        t.slicePalette[4]  = juce::Colour (0xFFA880C8);
        t.slicePalette[5]  = juce::Colour (0xFF58A098);
        t.slicePalette[6]  = juce::Colour (0xFFA88058);
        t.slicePalette[7]  = juce::Colour (0xFF708090);
        t.slicePalette[8]  = juce::Colour (0xFF5A8EB8);
        t.slicePalette[9]  = juce::Colour (0xFFAC6A7E);
        t.slicePalette[10] = juce::Colour (0xFF4E9468);
        t.slicePalette[11] = juce::Colour (0xFF9C8250);
        t.slicePalette[12] = juce::Colour (0xFF9674B8);
        t.slicePalette[13] = juce::Colour (0xFF5E9B96);
        t.slicePalette[14] = juce::Colour (0xFF9A7A56);
        t.slicePalette[15] = juce::Colour (0xFF6E7E90);
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
        t.moduleNamePlayback = juce::Colour (0xFF4A7098);
        t.moduleNameFilter   = juce::Colour (0xFF98783A);
        t.moduleNameAmp      = juce::Colour (0xFF4A7858);
        t.moduleNameOutput   = juce::Colour (0xFF685090);
        t.tabGlobalActive = juce::Colour (0xFF4878A0);
        t.tabSliceActive  = juce::Colour (0xFFA880C8);
        t.overrideBar     = juce::Colour (0xFFB84830);
        t.overrideBarHover = juce::Colour (0xFFD86050);
        t.overrideLabel   = juce::Colour (0xFFA04030);
        t.overrideValue   = juce::Colour (0xFFC0C8D0);
        t.overrideCount   = juce::Colour (0xFF904030);
        t.filterToggleOn  = juce::Colour (0xFF806838);
        t.setBpmText      = juce::Colour (0xFF48C0A8);
        t.setBpmBorder    = juce::Colour (0xFF183028);
        t.paramLabel      = juce::Colour (0xFF384048);
        t.paramValue      = juce::Colour (0xFF7888A0);
        t.paramValueOn    = juce::Colour (0xFF48C0A8);
        t.paramValueOff   = juce::Colour (0xFF181C24);
        t.contextBarBg    = juce::Colour (0xFFE0E0EC);
        t.signalChainBg   = juce::Colour (0xFF0A0C10);
        t.moduleBorder    = juce::Colour (0xFF161A20);
        t.contextText     = juce::Colour (0xFF5E6874);
        t.contextDimText  = juce::Colour (0xFF7A8692);
        t.tabInactive     = juce::Colour (0xFF7A8692);
        t.lazyChopOverlay = juce::Colour (0xFFCC4444);
        t.previewCursor   = juce::Colour (0xFFCC4444);
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
            else if (key == "module_name_playback") t.moduleNamePlayback = parseHex (val);
            else if (key == "module_name_filter")   t.moduleNameFilter = parseHex (val);
            else if (key == "module_name_amp")      t.moduleNameAmp = parseHex (val);
            else if (key == "module_name_output")   t.moduleNameOutput = parseHex (val);
            else if (key == "tab_global_active")    t.tabGlobalActive = parseHex (val);
            else if (key == "tab_slice_active")     t.tabSliceActive = parseHex (val);
            else if (key == "override_bar")         t.overrideBar = parseHex (val);
            else if (key == "override_bar_hover")   t.overrideBarHover = parseHex (val);
            else if (key == "override_label")       t.overrideLabel = parseHex (val);
            else if (key == "override_value")       t.overrideValue = parseHex (val);
            else if (key == "override_count")       t.overrideCount = parseHex (val);
            else if (key == "filter_toggle_on")     t.filterToggleOn = parseHex (val);
            else if (key == "set_bpm_text")         t.setBpmText = parseHex (val);
            else if (key == "set_bpm_border")       t.setBpmBorder = parseHex (val);
            else if (key == "param_label")          t.paramLabel = parseHex (val);
            else if (key == "param_value")          t.paramValue = parseHex (val);
            else if (key == "param_value_on")       t.paramValueOn = parseHex (val);
            else if (key == "param_value_off")      t.paramValueOff = parseHex (val);
            else if (key == "context_bar_bg")       t.contextBarBg = parseHex (val);
            else if (key == "signal_chain_bg")      t.signalChainBg = parseHex (val);
            else if (key == "module_border")        t.moduleBorder = parseHex (val);
            else if (key == "context_text")         t.contextText = parseHex (val);
            else if (key == "context_dim_text")     t.contextDimText = parseHex (val);
            else if (key == "tab_inactive")         t.tabInactive = parseHex (val);
            else if (key == "lazy_chop_overlay")    t.lazyChopOverlay = parseHex (val);
            else if (key == "preview_cursor")       t.previewCursor = parseHex (val);
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
        s << "module_name_playback: " << colourToHex (moduleNamePlayback) << "\n";
        s << "module_name_filter: " << colourToHex (moduleNameFilter) << "\n";
        s << "module_name_amp: " << colourToHex (moduleNameAmp) << "\n";
        s << "module_name_output: " << colourToHex (moduleNameOutput) << "\n";
        s << "tab_global_active: " << colourToHex (tabGlobalActive) << "\n";
        s << "tab_slice_active: " << colourToHex (tabSliceActive) << "\n";
        s << "override_bar: " << colourToHex (overrideBar) << "\n";
        s << "override_bar_hover: " << colourToHex (overrideBarHover) << "\n";
        s << "override_label: " << colourToHex (overrideLabel) << "\n";
        s << "override_value: " << colourToHex (overrideValue) << "\n";
        s << "override_count: " << colourToHex (overrideCount) << "\n";
        s << "filter_toggle_on: " << colourToHex (filterToggleOn) << "\n";
        s << "set_bpm_text: " << colourToHex (setBpmText) << "\n";
        s << "set_bpm_border: " << colourToHex (setBpmBorder) << "\n";
        s << "param_label: " << colourToHex (paramLabel) << "\n";
        s << "param_value: " << colourToHex (paramValue) << "\n";
        s << "param_value_on: " << colourToHex (paramValueOn) << "\n";
        s << "param_value_off: " << colourToHex (paramValueOff) << "\n";
        s << "context_bar_bg: " << colourToHex (contextBarBg) << "\n";
        s << "signal_chain_bg: " << colourToHex (signalChainBg) << "\n";
        s << "module_border: " << colourToHex (moduleBorder) << "\n";
        s << "context_text: " << colourToHex (contextText) << "\n";
        s << "context_dim_text: " << colourToHex (contextDimText) << "\n";
        s << "tab_inactive: " << colourToHex (tabInactive) << "\n";
        s << "lazy_chop_overlay: " << colourToHex (lazyChopOverlay) << "\n";
        s << "preview_cursor: " << colourToHex (previewCursor) << "\n";
        for (int i = 0; i < 16; ++i)
            s << "slice" << (i + 1) << ": " << colourToHex (slicePalette[i]) << "\n";
        return s;
    }
};
