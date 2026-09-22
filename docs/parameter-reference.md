---
title: Parameter reference
nav_order: 9
description: "Complete reference of every APVTS parameter INTERSECT exposes: ID, label, type, range, default, units."
---

# Parameter reference

This page lists every parameter INTERSECT exposes via its `AudioProcessorValueTreeState` (APVTS). These are the parameters your DAW sees for automation, Bitwig modulators, Ableton M4L, Reaper JSFX, etc.

For UI-only controls (NRPN settings, theme, scale, file-browser bookmarks and audition settings), see the [Settings file]({{ site.baseurl }}{% link settings-file.md %}) page — those persist in `settings.yaml`, not the plugin state.

Every parameter shown here is a **sample-wide default**. The signal chain bar's `SLICE` tab lets you override these per slice; per-slice overrides are stored in the saved session, not as APVTS parameters.

## Time & pitch

| ID | Label | Type | Range | Default | Units |
| --- | --- | --- | --- | --- | --- |
| `defaultBpm` | Sample BPM | float | 20 to 999 | 120 | BPM |
| `defaultPitch` | Sample Pitch | float | −48 to +48 | 0 | semitones |
| `defaultCentsDetune` | Sample Cents Detune | float | −100 to +100 | 0 | cents |
| `defaultAlgorithm` | Sample Algorithm | choice | `Repitch` / `Signalsmith` / `Bungee` | `Repitch` | — |
| `defaultRepitchMode` | Sample Repitch Mode | choice | `Linear` / `Cubic` | `Linear` | — |
| `defaultStretchEnabled` | Sample Stretch | bool | off / on | off | — |
| `defaultTonality` | Sample Tonality | float | 0 to 8000 | 0 | Hz |
| `defaultFormant` | Sample Formant | float | −24 to +24 | 0 | semitones |
| `defaultFormantComp` | Sample Formant Comp | bool | off / on | off | — |
| `defaultGrainMode` | Sample Grain Mode | choice | `Fast` / `Normal` / `Smooth` | `Normal` | — |

## Amp envelope & gain

| ID | Label | Type | Range | Default | Units |
| --- | --- | --- | --- | --- | --- |
| `defaultAttack` | Sample Attack | float | 0 to 1000 | 5 | ms |
| `defaultDecay` | Sample Decay | float | 0 to 5000 | 100 | ms |
| `defaultSustain` | Sample Sustain | float | 0 to 100 | 100 | % |
| `defaultRelease` | Sample Release | float | 0 to 5000 | 20 | ms |
| `masterVolume` | Master Gain | float | −100 to +24 | 0 | dB |

## Filter

| ID | Label | Type | Range | Default | Units |
| --- | --- | --- | --- | --- | --- |
| `defaultFilterEnabled` | Filter Enabled | bool | off / on | off | — |
| `defaultFilterType` | Filter Type | choice | `LP` / `HP` / `BP` / `NT` | `LP` | — |
| `defaultFilterSlope` | Filter Slope | choice | `12dB` / `24dB` | `12dB` | dB/oct |
| `defaultFilterCutoff` | Filter Cutoff | float | 20 to 20000 (log-skewed at 1 kHz) | 8200 | Hz |
| `defaultFilterReso` | Filter Resonance | float | 0 to 100 | 0 | % |
| `defaultFilterDrive` | Filter Drive | float | 0 to 100 | 0 | dB |
| `defaultFilterAsym` | Filter Drive Asymmetry | float | 0 to 100 | 0 | % |
| `defaultFilterKeyTrack` | Filter Key Track | float | 0 to 100 | 0 | % |
| `defaultFilterEnvAttack` | Filter Env Attack | float | 0 to 10000 | 0 | ms |
| `defaultFilterEnvDecay` | Filter Env Decay | float | 0 to 10000 | 0 | ms |
| `defaultFilterEnvSustain` | Filter Env Sustain | float | 0 to 100 | 100 | % |
| `defaultFilterEnvRelease` | Filter Env Release | float | 0 to 10000 | 0 | ms |
| `defaultFilterEnvAmount` | Filter Env Amount | float | −96 to +96 | 0 | semitones |

## Playback & mute

| ID | Label | Type | Range | Default | Units |
| --- | --- | --- | --- | --- | --- |
| `defaultReverse` | Sample Reverse | bool | off / on | off | — |
| `defaultLoop` | Sample Loop Mode | choice | `Off` / `Loop` / `Ping-Pong` | `Off` | — |
| `defaultCrossfade` | Sample Crossfade | float | 0 to 100 | 0 | % |
| `defaultOneShot` | Sample One Shot | bool | off / on | off | — |
| `defaultReleaseTail` | Sample Release Tail | bool | off / on | off | — |
| `defaultMuteGroup` | Sample Mute Group | int | 0 to 32 (0 = off) | 0 | group |

## Global utility

| ID | Label | Type | Range | Default | Units |
| --- | --- | --- | --- | --- | --- |
| `maxVoices` | Max Voices | int | 1 to 31 | 16 | voices |
| `uiScale` | UI Scale | float | 0.5 to 3.0 (step 0.25) | 1.0 | × |

## Notes

- **Per-slice overrides aren't APVTS parameters.** They're stored in the session state. If you want to automate a slice's pitch from your DAW, you do it by overriding `defaultPitch` and ensuring only the target slice plays at that moment.
- **`maxVoices` tops out at 31.** The 32nd voice is reserved for the preview voice used by sample auditioning and lazy chop.
- **`defaultFilterCutoff` is log-skewed at 1 kHz.** DAW automation curves should be log-shaped to feel musical.
- **Filter parameters resolve at note-on.** Automating cutoff mid-note affects the next triggered note, not the currently sounding one.
- **`defaultFilterEnvAmount` is in semitones**, not Hz, so `+12` means the envelope can push cutoff up by one octave regardless of the base cutoff.
- **The Signalsmith algorithm** was historically called "Stretch" internally. The UI and APVTS choice list use `Signalsmith`.
