# INTERSECT

[![Latest release](https://img.shields.io/github/v/release/tucktuckg00se/INTERSECT)](https://github.com/tucktuckg00se/INTERSECT/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/tucktuckg00se/INTERSECT/total)](https://github.com/tucktuckg00se/INTERSECT/releases)
[![License](https://img.shields.io/github/license/tucktuckg00se/INTERSECT)](LICENSE)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-informational)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20Standalone-informational)

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/yhNReDnQvE)
[![Docs](https://img.shields.io/badge/docs-read%20online-0A7BBB?logo=readthedocs&logoColor=white)](https://tucktuckg00se.github.io/INTERSECT/)
[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-EA4AAA?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/tucktuckg00se)
[![Buy me a coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/tucktuckgoose)

INTERSECT is a free, open-source sample slicer with stem separation built in. It runs as VST3, AU or a standalone app on Windows, macOS and Linux.

![INTERSECT screenshot](.github/assets/screenshot.png)
*Theme shown: Open Color (`oc.intersectstyle`)*

## Features

<details>
<summary><strong>Stem separation</strong>: split any sample into stems without leaving the plugin</summary>

- Four models, from fast 4-stem splits (drums, bass, vocals, other) to 53 individual instruments
- Runs on your own CPU or GPU (DirectML, CoreML, CUDA, MIGraphX), so your audio never leaves your machine
- Stems land in your session as new samples, starting with their source sample's settings
- Export one file per stem, or combine stems into one (say, everything but the vocals)

Docs: [Stem separation](https://tucktuckg00se.github.io/INTERSECT/stem-separation/)

</details>

<details>
<summary><strong>Chopping</strong>: slice by hand, by ear or by transient</summary>

- Draw slices on the waveform
- Lazy chop: play the sample and tap keys in time to drop slices
- Auto chop: split a slice at detected transients (previewed before you apply) or into equal parts
- Snap to zero crossings, and resequence slice notes by position or creation order
- REX2 loops arrive already sliced, at their original tempo

Docs: [Slices](https://tucktuckg00se.github.io/INTERSECT/workflow-basics/#slices)

</details>

<details>
<summary><strong>Time and pitch</strong>: keep loops in time with your DAW, or play them across the keyboard</summary>

- Three engines, chosen per sample: Repitch, Signalsmith and Bungee
- `STRETCH` syncs a sample to your DAW tempo; Signalsmith and Bungee keep pitch independent of speed
- `SET BPM` works out a loop's tempo from its length ("this is 2 bars")
- Slice note ranges play a slice chromatically across a span of keys

Docs: [Time and pitch algorithms](https://tucktuckg00se.github.io/INTERSECT/workflow-basics/#time-and-pitch-algorithms)

</details>

<details>
<summary><strong>Sound shaping</strong>: set a sound once per sample, then lock any setting on a single slice</summary>

- Every slice inherits its sample's settings until you lock one of its own
- Per-voice multimode filter with its own envelope, key tracking and drive
- Amp envelope, gain, pitch and fine tune
- Loop or ping-pong with crossfade, reverse, one-shot

Docs: [The inheritance / lock model](https://tucktuckg00se.github.io/INTERSECT/workflow-basics/#the-inheritance--lock-model)

</details>

<details>
<summary><strong>Sessions and workflow</strong>: a whole kit in one instance, from browser to DAW</summary>

- Up to 64 samples per session, with 31 voices, mute groups and 16 stereo outputs
- Built-in file browser with search, bookmarks and preview
- Import from URL: paste a YouTube, SoundCloud or Bandcamp link (or one from any other site yt-dlp supports) and the audio lands in your kit as a WAV or FLAC
- Presets save the whole kit, optionally with the samples embedded for sharing
- MIDI export: drag a slice pattern into your DAW as a MIDI clip
- Loads WAV, AIFF, FLAC, OGG, MP3 and REX2; missing files relink in one step; full undo/redo

Docs: [Interface](https://tucktuckg00se.github.io/INTERSECT/interface/) · [Import from URL](https://tucktuckg00se.github.io/INTERSECT/interface/#import-from-url) · [Presets](https://tucktuckg00se.github.io/INTERSECT/presets/)

</details>

<details>
<summary><strong>Control and looks</strong>: hardware encoders, themes and scaling</summary>

- Edit slice boundaries and zoom from a MIDI controller's endless encoders (NRPN)
- Eight bundled themes, plus your own theme files
- UI scales from 0.5× to 3×

Docs: [NRPN MIDI routing](https://tucktuckg00se.github.io/INTERSECT/nrpn-midi/) · [Themes](https://tucktuckg00se.github.io/INTERSECT/themes/)

</details>

## Quick Start

[Watch the Quick Start Guide on YouTube](https://youtu.be/zsdtyIff2PQ)

## Install

Download the latest release zip for your system from [Releases](https://github.com/tucktuckg00se/INTERSECT/releases) (Windows and Linux come in `x64` and `arm64`; macOS in `arm64` and `x64`) and copy the plugin files into your system plugin folder.

| Format | Windows | macOS | Linux |
| --- | --- | --- | --- |
| VST3 | `C:\Program Files\Common Files\VST3\` | `~/Library/Audio/Plug-Ins/VST3/` | `~/.vst3/` |
| AU | n/a | `~/Library/Audio/Plug-Ins/Components/` | n/a |

After copying, rescan plugins in your DAW.

**macOS first launch:** release builds are unsigned, so macOS may report INTERSECT as "damaged" on first launch. Clear quarantine flags from a Terminal:

```bash
xattr -cr ~/Library/Audio/Plug-Ins/VST3/INTERSECT.vst3
xattr -cr ~/Library/Audio/Plug-Ins/Components/INTERSECT.component
xattr -cr /Applications/INTERSECT.app
```

For per-platform release contents, ONNX Runtime bundles for stem separation, and GPU runtime requirements, see the [Installation guide](https://tucktuckg00se.github.io/INTERSECT/installation/).

## Build

```bash
git clone --recursive https://github.com/tucktuckg00se/INTERSECT.git
cd INTERSECT
cmake -B build
cmake --build build --config Release
```

Requires CMake 3.22+, a C++20 compiler, and Git. For per-OS toolchain setup, build outputs, the release workflow, and the dependency list, see [Build from source](https://tucktuckg00se.github.io/INTERSECT/building-from-source/).

## License

INTERSECT is licensed under the [GNU General Public License v3.0](LICENSE). It is built with [JUCE](https://juce.com), used under the AGPLv3, and other open-source libraries and the Inter typeface; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Every release zip includes both files.

REX2 import uses [VelociLoops](https://github.com/kunitoki/VelociLoops) (public domain, Unlicense). REX2 import and MIDI export were originally contributed by [@cadecomposer](https://github.com/cadecomposer).

## Support / Known limitations

- INTERSECT project recall stores sample file paths for every file in the session; if files move, relink is required.
- Builds are unsigned; platform security prompts (especially macOS) may require manual trust/quarantine removal.
- Report bugs or request features via [GitHub Issues](https://github.com/tucktuckg00se/INTERSECT/issues).
