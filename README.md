# INTERSECT

A JUCE-based audio sampler plugin that loads a single sample, slices it into regions, and triggers slices via MIDI with independent per-slice parameter control.

![INTERSECT screenshot](https://raw.githubusercontent.com/tucktuckg00se/INTERSECT/master/docs/screenshot.png)

## Features

- **Drag-and-drop sample loading** — WAV, OGG, AIFF, FLAC
- **Slice-based playback** — create regions with start/end points, each mapped to a MIDI note
- **Parameter inheritance** — slices inherit sample-level defaults (BPM, pitch, ADSR, mute group, etc.) unless individually locked/overridden
- **Three stretch algorithms:**
  - **Repitch** — classic sample-rate manipulation where pitch and speed are linked
  - **Stretch** — independent pitch and time control via [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch), with tonality, formant shift, and formant compensation controls
  - **Bungee** — grain-based time-stretch via [Bungee](https://github.com/bungee-audio-stretch/bungee), with adjustable grain mode (Fast / Normal / Smooth)
- **Lazy chop** — play the sample continuously and place slice boundaries in real time by pressing MIDI keys
- **SET BPM** — calculate BPM from a slice length and a musical time unit (4 bars down to 1/32 bar)
- **Mute groups** — voices in the same group cut each other off
- **MIDI-selects-slice** — optionally auto-select a slice in the UI when its MIDI note is played
- **Duplicate slice** — clone a slice with all its locked parameters
- **Hi-DPI scaling** — adjustable UI scale factor (0.5x to 3x)
- **Full state recall** — all parameters, slices, and audio data saved/restored with the DAW session
- **Dark industrial theme** — styled popup menus and controls

## Build

Requires CMake 3.22+ and a C++20 compiler.

```bash
git clone --recursive git@github.com:tucktuckg00se/INTERSECT.git
cd INTERSECT
cmake -B build -S project
cmake --build build --config Release
```

Builds VST3, AU (macOS), and Standalone targets.

## Dependencies

- [JUCE](https://github.com/juce-framework/JUCE) (git submodule)
- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (git submodule, MIT license)
- [Bungee](https://github.com/bungee-audio-stretch/bungee) (git submodule, MPL-2.0 license)

## USE AT YOUR OWN RISK
This plugin is 100% vibe-coded

## License

All rights reserved.
