# Changelog

All notable changes to INTERSECT will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- GPU-accelerated stem separation via downloadable ONNX Runtime bundles. Open SET > Stem Separation > ONNX Runtime and pick NVIDIA CUDA 12 or 13 / AMD MIGraphX on Linux, DirectML on Windows, or CoreML on macOS. Bundles download into your user data folder; restart INTERSECT after install to use the new runtime.

### Changed
- Updated the ONNX Runtime this build targets to 1.24.2 on Windows and Linux; macOS stays on 1.23.2 for x64 support.
- On Intel macs, the stem separation panel now clearly shows that stem separation is not available instead of failing on START.

### Fixed
- GPU stem separation errors now display in the header status bar instead of silently doing nothing.
- ONNX Runtime and stem model downloads no longer do completion work on the audio thread, removing a potential source of audio dropouts during downloads.

## [0.13.0] - 2026-04-15

### Added
- Multi-sample sessions with APPEND loading, a dedicated sample lane, sample reordering, and sample deletion
- Stem separation powered by BS-RoFormer: split any sample into vocals and instrumental via the STEM panel

### Changed
- Sample lane colors now run opposite the slice palette, labels truncate cleanly, and slice/sample lane blocks use a simpler filled style
- Stem export now shows a `CANCEL` button while separation is running, and header status text copies warning/error messages instead of opening sample load dialogs

### Fixed
- Restoring multi-sample projects and undoing back to an empty session no longer leaves stale sample data loaded
- Stem export now opens reliably from the sample lane, warning banners clear automatically, and copied release builds bundle the ONNX Runtime files they need
- Hosted VST3 stem separation is now more stable in Ableton Live and REAPER on Windows by forcing INTERSECT to use its bundled ONNX Runtime instead of incompatible DAW-loaded versions

## [0.12.4] - 2026-04-12

### Added
- Middle C octave convention setting in the SET menu: choose C3, C4, or C5 to match your DAW's note naming (saved globally, does not affect stored MIDI data)

### Fixed
- RESEQ no longer collapses note-range slices to single notes; ranges and root note offsets are preserved, and slices are packed without overlaps
- Warning now appears when creating or resequencing slices past the MIDI note limit, showing which slices are affected
- Auto chop no longer freezes the UI on large samples; transient analysis now runs in the background with responsive slider controls

## [0.12.3] - 2026-04-11

### Changed
- Standalone builds now let you set the transport BPM from the Audio/MIDI settings dialog

## [0.12.2] - 2026-04-04

### Changed
- Improved internal stability and maintenance in the waveform, slice, and signal-chain editor code

### Fixed
- Reopened projects now keep sample speed and pitch consistent when the project sample rate differs from the sample file

## [0.12.1] - 2026-04-01

### Changed
- Empty sample states now use clearer loading instructions, and Linux Wayland/XWayland sessions no longer suggest drag-and-drop when it is unavailable

### Fixed
- Linux builds now load MP3 files through INTERSECT's sample browser

## [0.12.0] - 2026-04-01

### Added
- Loop crossfade (FADE) parameter — smooths loop and ping-pong seams with equal-power crossfading, adjustable from 0–100% of the slice length
- Fade region overlay and crossfade source cursor in the waveform view
- Repitch MODE parameter with Linear and Cubic options for higher-quality Repitch playback
- Slice note ranges with `NOTE`/`RANGE` switching plus per-slice `LOW`, `HIGH`, and `ROOT` note controls for chromatic trigger zones and transposition

### Changed
- Gain control moved from the Playback module to the Amp module in the signal chain
- Renamed internal code to match UI labels: "Time/Pitch" and "Playback" modules, "Settings" button
- Loop points now use dedicated waveform handles

### Fixed
- Header action buttons now use theme-coloured outlines, and OFF states in light themes are easier to read
- Bungee algorithm no longer produces loud static when Grain is set to Smooth
- Fade overlay now updates immediately when changing global crossfade, loop mode, or reverse settings
- Loop fade cursors now appear on the first loop or ping-pong seam, and saved projects once again restore their linked sample path correctly
- Context-bar note editing no longer flips single-note slices into range mode while dragging, and note names now stay read-only beside the numeric controls

## [0.11.0] - 2026-03-31
### Added
- Filter drive asymmetry (ASYM) parameter — biases the drive waveshaper to add even-harmonic saturation for more tonal variety
- "Buy Me a Coffee" sponsor link in the Settings menu

### Changed
- Stretch algorithm renamed to "Signalsmith" in the algorithm selector
- Signal chain modules use consistent cell sizing and alignment across all rows
- Auto Chop controls (SENS, MIN, DIV) now use drag-to-edit cells with double-click text entry
- Transient detection uses spectral flux analysis — SENS controls detection threshold (0–100), MIN controls minimum slice length in ms
- Action tooltips are shorter, and RESEQ now has a Shift+R shortcut

### Fixed
- Crash when quitting certain DAWs on macOS (e.g. Cubase, Live) caused by typeface cleanup during static destruction
- Filter row 2 cells no longer drift out of alignment with row 1
- Filter key tracking now scales musically across the keyboard (previously over-tracked upward and under-tracked downward)
- Filter resonance response is more evenly distributed across the knob range
- Filter cutoff automation in the DAW now follows a logarithmic curve matching how we hear pitch
- Slice numbers no longer drift outside their region when zoomed out with many slices
- Signal chain background color is now consistent between global and slice tabs
- Auto Chop transient markers now land at the true onset instead of slightly after
- Undo now restores the pre-drag value in one step instead of stepping through intermediate values
- Global parameter changes (BPM, pitch, filter, etc.) are now undoable
- Root note changes are now undoable
- Undo no longer couples unrelated slice and global parameter changes

### Removed
- Removed unused SliceControlBar and SettingsPanel source files

## [0.10.0] - 2026-03-20
### Added
- Multimode filter with per-voice processing (type, slope, cutoff, resonance, drive, key tracking, filter ADSR envelope)
- New bundled colour themes
- RESEQ action to re-sequence MIDI notes across slices (v0.10.9)

### Changed
- Tightened envelope timing controls (attack/decay/release ranges)
- Renamed signal chain modules; moved MUTE to row 2 (v0.10.5)
- Refined popup menus and parameter editing (v0.10.2)
- Simplified signal chain UI and adjusted theme colours (v0.10.2)
- Switched voice mixing to block render paths (v0.10.7)

### Fixed
- AU cold open failure on older macOS (v0.10.10, issue #13)
- Bungee loop buzzing with unbounded phase model (v0.10.8)
- Ping-pong direction tracking for stretch engines (v0.10.8)
- Stretch engine seam resets replaced with exact virtual loop feeding (v0.10.8)
- Sample restore timeline drift (v0.10.6)
- Global SET BPM now uses selected slice range (v0.10.4)
- Text editor Enter-key crash — async teardown regression (v0.10.3)
- Text-entry crash fix restored after GUI rewrite (v0.10.1)
- Duplicate MIDI notes on slice creation (v0.10.9)
- Clang/GCC build compatibility for juce::File assignment (v0.10.7)
- State restore and real-time safety hardening (v0.10.7)

## [0.9.0] - 2026-02-28
### Added
- NRPN MIDI controller routing
- SET popup for BPM/pitch assignment
- .aif file extension support (v0.9.1)
- Auto-detect plugin version from git tag (v0.9.2)

### Fixed
- Text editor use-after-free crash (v0.9.3)
- FontOptions constructor compatibility (v0.9.2)
- Windows CI version string parsing (bash shell fix)

## [0.8.0] - 2026-02-18
### Added
- Filled waveform rendering with peak mipmaps
- Theme-aware slice UI and colour palette
- One-shot (1SHOT) playback mode (v0.8.7)
- Output safety clamp and NaN/Inf guard (v0.8.5)
- macOS Intel x64 build (v0.8.4)
- Button tooltips and keyboard shortcuts (v0.8.3)
- Async sample loading with tune controls (v0.8.8)
- Live slice drag preview and smart repaint scheduling (v0.8.10)
- Shift+key shortcuts and overlay hint system (v0.8.14)
- Lazy chop real-time slice placement (v0.8.11)
- Pitch range extended from ±24 to ±48 semitones (v0.8.12)

### Changed
- Merged pingPong + loop into 3-way loop mode (v0.8.1)
- Removed WSOLA algorithm; renamed DAW parameters (v0.8.6)
- Follow MIDI now works during lazy chop (v0.8.6)
- STRETCH control moved to row 1 (v0.8.7)
- HeaderBar always visible (v0.8.11)

### Fixed
- Null pointer crash on sample load (v0.8.2)
- macOS release build failures (v0.8.9)
- MIDI note collision on slice split (v0.8.13)
- Redo functionality (v0.8.3)
- MIDI All Notes Off / All Sound Off handling (v0.8.7)
- Click-to-cycle parameter choices (v0.8.2)

## [0.7.0] - 2026-02-17
### Added
- Theme-aware slice colour palette (v0.7.1)
- `.intersectstyle` theme file extension (v0.7.1)

### Changed
- Repository flattened to single-project structure
- Theme colour naming standardized
- Release zip naming updated

### Fixed
- Undo state restoration
- Button theming (v0.7.1)

## [0.6.0] - 2026-02-17
### Added
- Auto-chop with transient detection
- Undo/redo system

### Changed
- Auto-chop panel redesigned with sensitivity slider (v0.6.1)

### Fixed
- Transient detection accuracy (v0.6.1)

## [0.5.0] - 2026-02-17
### Added
- Multi-output bus routing
- Reverse (REV) playback mode
- Multi-slice simultaneous triggering
- Voice count display
- Sub-sample zoom waveform rendering (v0.5.2)

### Changed
- Wider plugin window

### Fixed
- Bungee ping-pong playback (v0.5.1)
- Lazy chop preview in multi-output mode (v0.5.2)
- Sample replace crash (v0.5.1)

## [0.4.0] - 2026-02-16
### Added
- Gain parameter
- Release tail control
- Ping-pong mode for all stretch algorithms

### Fixed
- Blank-space drag behaviour (v0.4.1)
- Button overflow in narrow layouts (v0.4.1)
- Bungee ping-pong audio pop (v0.4.1)
- Sample relink after file move

## [0.3.0] - 2026-02-15
### Changed
- Improved font rendering sharpness
- Enlarged UI fonts and controls
- SliceLane visually separated from waveform
- SliceControlBar layout reorganized

## [0.2.0] - 2026-02-15
### Added
- Lazy chop with time-stretch preview
- Path-based sample state restoration
- Missing sample relink dialog

### Fixed
- Button ordering in action panel

## [0.1.0] - 2026-02-15
### Added
- VST3, AU, and Standalone audio sampler plugin
- Sample loading via drag-and-drop (WAV, OGG, AIFF, FLAC, MP3)
- Manual slice creation with per-slice MIDI note assignment
- Per-slice parameter control with lock/inherit system
- Signalsmith Stretch and Bungee time/pitch processing
- WSOLA stretch algorithm
- ADSR envelope per voice
- Mute groups
- Waveform display with scroll, zoom, and cursor-anchored zoom
- Lazy chop with preview highlight
- YAML theme support with IBM Plex Sans font
- LOAD button and draw preview mode
- Root note control and UI scale persistence
- Cross-platform CI/CD release workflow (Windows, macOS, Linux)
