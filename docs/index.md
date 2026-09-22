---
title: Home
layout: home
nav_order: 1
description: "INTERSECT is a sample slicer instrument plugin (VST3/AU/Standalone) with multi-sample sessions, per-slice locking, slice note ranges, multiple time/pitch algorithms, and MIDI-triggered slice playback."
permalink: /
---

# INTERSECT

INTERSECT is a sample slicer instrument plugin for VST3, AU, and Standalone. Load one or many audio files into a session, chop them into slices, and trigger each slice from MIDI. Slices inherit sample-wide defaults but can lock any parameter — pitch, filter, envelope, loop, output bus — to override that default per slice. Three time/pitch engines (Repitch, Signalsmith, Bungee) give you independent control over speed and pitch, and on-demand GPU-accelerated stem separation lets you split a sample into drums, bass, vocals, and other parts without leaving the plugin.

![INTERSECT screenshot](https://raw.githubusercontent.com/tucktuckg00se/INTERSECT/main/.github/assets/screenshot.png)
*Theme shown: Open Color (`oc.intersectstyle`)*

[Watch the Quick Start Guide on YouTube](https://youtu.be/zsdtyIff2PQ){: .btn .btn-primary }
[Download a release](https://github.com/tucktuckg00se/INTERSECT/releases){: .btn }
[GitHub repo](https://github.com/tucktuckg00se/INTERSECT){: .btn }

---

## New here?

Start with the **[Getting started]({{ site.baseurl }}{% link getting-started.md %})** walkthrough — it takes you from an empty editor to a sliced, MIDI-triggered sample in about 10 minutes.

## Documentation

### Tutorials
- [Getting started]({{ site.baseurl }}{% link getting-started.md %}) — your first session in ~10 minutes
- [Stem separation]({{ site.baseurl }}{% link stem-separation.md %}) — split a sample into drums, bass, vocals, and more

### Concepts & interface
- [Concepts]({{ site.baseurl }}{% link workflow-basics.md %}) — multi-sample sessions, slice creation, inheritance, algorithms
- [Interface]({{ site.baseurl }}{% link interface.md %}) — header bar, file browser, lanes, signal chain
- [Controls and shortcuts]({{ site.baseurl }}{% link controls-reference.md %}) — every control, every keyboard shortcut

### Reference
- [Parameter reference]({{ site.baseurl }}{% link parameter-reference.md %}) — every APVTS parameter with range, default, units
- [Settings file]({{ site.baseurl }}{% link settings-file.md %}) — what gets persisted between sessions and where
- [Themes]({{ site.baseurl }}{% link themes.md %}) — `.intersectstyle` file format and colour-key reference
- [NRPN MIDI routing]({{ site.baseurl }}{% link nrpn-midi.md %}) — hardware controller setup for slice editing
- [Changelog]({{ site.baseurl }}{% link changelog.md %}) — what's new in each release

### Operations
- [Installation]({{ site.baseurl }}{% link installation.md %}) — release contents, plugin paths, stem-separation runtimes, GPU requirements, macOS quarantine
- [Troubleshooting]({{ site.baseurl }}{% link troubleshooting.md %}) — symptom-first FAQ
- [Build from source]({{ site.baseurl }}{% link building-from-source.md %}) — per-OS toolchain setup and dependencies

## Support development

- [Sponsor on GitHub](https://github.com/sponsors/tucktuckg00se)
- [Buy me a coffee](https://buymeacoffee.com/tucktuckgoose)

## License

INTERSECT is licensed under the [GNU General Public License v3.0](https://github.com/tucktuckg00se/INTERSECT/blob/main/LICENSE).
