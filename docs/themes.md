---
title: Themes
nav_order: 11
description: "Author custom .intersectstyle theme files for INTERSECT — full reference of every colour key, bundled themes, and the file format."
---

# Themes

INTERSECT loads custom `.intersectstyle` theme files from a per-user themes folder. On first launch it writes default `dark.intersectstyle` and `light.intersectstyle` files there so you always have a working example to copy.

## Where themes live

| OS | Themes folder |
| --- | --- |
| Windows | `%APPDATA%\Roaming\INTERSECT\themes\` |
| macOS | `~/Library/Application Support/INTERSECT/themes/` |
| Linux | `~/.config/INTERSECT/themes/` |

INTERSECT creates this folder automatically on first launch. Themes you save here appear under **Theme** on the **General** page of **SET** in the header bar.

## Quick start

1. Open the themes folder for your OS.
2. Copy `dark.intersectstyle` (or any other file in the folder) and rename it, for example `mytheme.intersectstyle`.
3. Open it in a text editor, change the `name:` value, and edit the colour values.
4. **Restart the plugin.** Themes are read once at startup.
5. Pick your theme in **SET → General → Theme**.

## File format

The theme file is a plain-text, line-oriented format. Each line is either blank, a comment, or a `key: value` pair.

```text
# This is a comment line
name: mytheme
surface0: 060608     # inline comment after two spaces + '#'
```

Rules:
- One setting per line: `key: value`.
- Colours are written as **6-digit hexadecimal RGB**: `RRGGBB`. No leading `#`, no alpha channel.
- Lines starting with `#` are ignored.
- Inline comments must be preceded by a space + `#` (`  # like this`).
- Empty lines are ignored.
- Values may optionally be quoted; quotes are stripped.

If a key is missing, INTERSECT uses the value from the default dark theme. You only need to define the keys you want to override — but the bundled themes always include every key so you can see the full picture.

## Colour key reference

The theme file exposes 32 colour keys plus the `name:` field, grouped into five categories.

### Surfaces (6)

Surfaces form the background hierarchy. In dark themes they progress from darkest to lightest; reverse the direction for light themes.

| Key | Where you see it |
| --- | --- |
| `surface0` | Waveform pit (the area behind the rendered waveform) |
| `surface1` | Base application background |
| `surface2` | Raised panels: popup menus, overlays, dropdowns |
| `surface3` | Borders and separators |
| `surface4` | Recessed controls: parameter cells, unpressed buttons |
| `surface5` | Hover state, separator highlight |

### Text (3)

Text colours form a contrast ladder from muted to primary.

| Key | Where you see it |
| --- | --- |
| `text0` | Muted/inactive text: disabled buttons, hints, dim labels |
| `text1` | Body text: parameter values, secondary labels |
| `text2` | Primary text: main labels, active text |

### Waveform & accent (2)

| Key | Where you see it |
| --- | --- |
| `waveform` | Audio peak rendering colour |
| `accent` | Selection overlay, active tabs, highlighted menu items, draw-slice preview |

### Module colours (5)

Each signal-chain module gets its own header tint. `color5` doubles as the unified alert/lock colour.

| Key | Where you see it |
| --- | --- |
| `color1` | Time/Pitch (and Playback) module header |
| `color2` | Filter module header |
| `color3` | Amp module header |
| `color4` | Output module header |
| `color5` | Alert/warning state, lock indicator, override marker, preview cursor |

### Slice palette (16)

`slice1` through `slice16` colour individual slice regions in the waveform and slice lane. Slices cycle through the palette in creation order. Pick 16 visually distinct values so adjacent slices don't blur together.

## Sample template

A fully-commented starting file:

```text
# INTERSECT Custom Theme — minimal template
#
# Themes folder:
#   Windows:  %APPDATA%\Roaming\INTERSECT\themes\
#   macOS:    ~/Library/Application Support/INTERSECT/themes/
#   Linux:    ~/.config/INTERSECT/themes/
#
# Colours are 6-digit hex (RRGGBB). Restart the plugin to apply changes.

name: mytheme

# Surface hierarchy (6 levels, darkest → lightest in dark themes)
surface0: 060608      # waveform pit
surface1: 0a0a0e      # base background
surface2: 0e0e13      # raised panels
surface3: 181c24      # borders
surface4: 23232d      # recessed controls
surface5: 2a3040      # hover / separator

# Text hierarchy (3 levels, muted → primary)
text0: 506070
text1: 7888a0
text2: ccd0d8

# Waveform + accent
waveform: b2c6d8
accent:   3fd8d8

# Module headers (color5 also drives alert/lock indicators)
color1: 4a7098        # time/pitch + playback
color2: 98783a        # filter
color3: 4a7858        # amp
color4: 685090        # output
color5: cc4444        # alert / warning / lock

# Slice palette (16 distinct values — cycled per slice)
slice1:  4d8c99
slice2:  8c4747
slice3:  4d8059
slice4:  8c7340
slice5:  664d8c
slice6:  80804d
slice7:  40808c
slice8:  804d6b
slice9:  597a47
slice10: 80594d
slice11: 52598c
slice12: 737359
slice13: 6b4773
slice14: 477a6b
slice15: 7a5973
slice16: 617a66
```

## Bundled themes

Eight themes ship with INTERSECT in [`themes/`](https://github.com/tucktuckg00se/INTERSECT/tree/main/themes) — copy any of them as a starting point.

| File | Style |
| --- | --- |
| `dark.intersectstyle` | The default dark theme |
| `light.intersectstyle` | The default light theme |
| `bitwig-default.intersectstyle` | Mimics Bitwig Studio's track/clip palette |
| `callisto.intersectstyle` | Monochrome teal-accented dark |
| `koda.intersectstyle` | Vivid dark, based on [koda.nvim](https://github.com/oskarnurm/koda.nvim) |
| `oc.intersectstyle` | Bright high-contrast, based on [Open Color](https://github.com/yeun/open-color) |
| `rose-pine.intersectstyle` | Muted purples/golds in the Rose Pine spirit |
| `studio98.intersectstyle` | Retro light theme with a Windows 98 flavour |

## Legacy keys

Earlier versions of INTERSECT used a longer list of purpose-named keys (`background:`, `foreground:`, `lockActive:`, `module_name_filter:`, etc.). Those keys still load — themes written for older builds continue to work — but new themes should use the modern keys above.

If both the modern and legacy form appear in the same file, the modern key wins.

<details markdown="block">
<summary>Legacy → modern key mapping</summary>

| Legacy key(s) | Modern key |
| --- | --- |
| `waveformBg` | `surface0` |
| `background`, `context_bar_bg`, `signal_chain_bg` | `surface1` |
| `darkBar`, `header` | `surface2` |
| `module_border`, `param_value_off`, `set_bpm_border` | `surface3` |
| `gridLine`, `button` | `surface4` |
| `separator`, `buttonHover` | `surface5` |
| `lockInactive`, `param_label`, `context_text`, `context_dim_text`, `tab_inactive` | `text0` |
| `param_value`, `override_value` | `text1` |
| `foreground` | `text2` |
| `selectionOverlay`, `set_bpm_text`, `param_value_on`, `tab_global_active`, `tab_slice_active` | `accent` |
| `module_name_playback` | `color1` |
| `module_name_filter`, `filter_toggle_on` | `color2` |
| `module_name_amp` | `color3` |
| `module_name_output` | `color4` |
| `lockActive`, `override_bar`, `override_bar_hover`, `override_label`, `override_count`, `lazy_chop_overlay`, `preview_cursor` | `color5` |

</details>

## Authoring tips

- **Restart to apply.** Theme files are parsed at plugin startup. After editing, close and reopen the plugin (or restart the standalone) to see your changes.
- **Test against a busy session.** Load a sample with 10+ slices so you exercise the full slice palette, then exercise menus, hover states, and an active filter so you cover every surface and module colour.
- **Watch contrast.** `text0` should be readable against `surface4`; `text2` against `surface1` and `surface2`. The bundled themes are calibrated, so use them as a reference if your custom theme feels muddy.
- **Share a theme.** Open a [pull request](https://github.com/tucktuckg00se/INTERSECT/pulls) adding your file to `themes/`, or attach it to a [GitHub issue](https://github.com/tucktuckg00se/INTERSECT/issues).

## Related

- [Settings file]({{ site.baseurl }}{% link settings-file.md %}) — where the active theme name is persisted alongside other user preferences.
- [Troubleshooting → Themes]({{ site.baseurl }}{% link troubleshooting.md %}#themes) — common theme-loading issues.
