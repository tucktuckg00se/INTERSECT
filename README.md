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

INTERSECT is a sample slicer instrument plugin (VST3/AU/Standalone) with multi-sample sessions, per-slice locking, slice note ranges, multiple time/pitch algorithms, and MIDI-triggered slice playback.

![INTERSECT screenshot](.github/assets/screenshot.png)
*Theme shown: Open Color (`oc.intersectstyle`)*

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

INTERSECT is licensed under the [GNU General Public License v3.0](LICENSE).

REX2 import uses [VelociLoops](https://github.com/kunitoki/VelociLoops) (public domain, Unlicense). REX2 import and MIDI export were originally contributed by [@cadecomposer](https://github.com/cadecomposer).

## Support / Known limitations

- INTERSECT project recall stores sample file paths for every file in the session; if files move, relink is required.
- Builds are unsigned; platform security prompts (especially macOS) may require manual trust/quarantine removal.
- Report bugs or request features via [GitHub Issues](https://github.com/tucktuckg00se/INTERSECT/issues).
