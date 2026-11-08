---
title: NRPN MIDI routing
nav_order: 8
description: "Configure a hardware MIDI controller to edit slice boundaries and zoom in INTERSECT via NRPN."
---

# MIDI controller routing (NRPN)

INTERSECT supports NRPN-based slice editing from a hardware or software MIDI controller. Enable it on the **MIDI** page of the **SET** view in the header bar, which also has the channel and consume options.

## Controller requirements

Your controller must:

- Have **endless rotary encoders** (not fixed-range knobs)
- Support **NRPN mode** with configurable MSB/LSB address values
- Send **relative** data bytes — CC 96 (Data Increment) and CC 97 (Data Decrement) — one step per encoder click
- Be set to send MSB **64** plus the per-parameter LSB from the [table below](#nrpn-parameter-table)

Controllers that send only absolute values (CC 6 Data Entry) **will not work**, nor will standard fixed-range knobs. Confirmed-working examples include the Akai MPD32 and MPD218. Results may vary with other controllers — anything that meets the four requirements above should work.

## How a session looks

1. Turn on **NRPN editing** in **SET → MIDI**, and set the channel (`OMNI` = all channels).
2. Optionally turn on **Consume NRPN CCs** so the NRPN edit CCs don't pass through to downstream instruments.
3. Turn on `FM` (Follow MIDI) so playing a slice's MIDI note auto-selects it.
4. Play the slice you want to edit. INTERSECT selects it; the start/end knobs now target this slice.
5. Turn the start/end encoders to nudge slice boundaries. Each step moves `viewWidth / 16383` samples — **zoom in for finer control**.
6. The edit commits automatically about 300 ms after the encoder stops moving — the same feel as releasing a parameter slider, so undo captures the whole gesture as one step.

## NRPN parameter table

NRPN numbers use CC 99 (MSB address) / CC 98 (LSB address) to select the parameter, then CC 96 (Data Increment) or CC 97 (Data Decrement) to send a ±1 step. No absolute-value data bytes (CC 6/38) are used.

When programming a hardware controller, set the knob to **NRPN mode** with **MSB 64** and the LSB from this table:

| NRPN | MSB | LSB | Name | Direction | Notes |
| --- | --- | --- | --- | --- | --- |
| 8193 | 64 | 1 | Zoom | CC 96 / CC 97 | Zoom in / out |
| 8194 | 64 | 2 | Slice start | CC 96 / CC 97 | Each step = viewWidth / 16383 samples |
| 8195 | 64 | 3 | Slice end | CC 96 / CC 97 | Each step = viewWidth / 16383 samples |

## Example: Akai MPD218

The MPD218 has 16 endless rotary encoders organized in 4 banks of 4. A workable mapping is to dedicate the first three encoders on bank A:

| Encoder | Type | MSB | LSB | What it does |
| --- | --- | --- | --- | --- |
| Knob 1 | NRPN | 64 | 1 | Zoom in/out around the cursor |
| Knob 2 | NRPN | 64 | 2 | Nudge selected slice's start |
| Knob 3 | NRPN | 64 | 3 | Nudge selected slice's end |

Set each encoder's MIDI channel to match the channel in **SET → MIDI** (or use `OMNI` and leave the controller on channel 1). Edit the MPD218 program in the Akai MPC software, change those three knobs from CC to NRPN, set the addresses, and store the program.

## SET button — NRPN settings

Open the **SET** view from the header bar and choose **MIDI**.

| Control | Function |
| --- | --- |
| `NRPN EDITING` | Enable/disable NRPN slice editing |
| `CONSUME NRPN CCs` | Strip NRPN edit CCs from MIDI output so they don't reach downstream instruments |
| `CHANNEL` `−` / `+`, `OMNI` | MIDI channel filter (`OMNI` = all channels, saved as `0`) |

Settings are saved to `settings.yaml` alongside theme and UI scale — see the [Settings file]({{ site.baseurl }}{% link settings-file.md %}) reference.

## DAW-specific notes

**Ableton Live:** NRPN control does not work in Ableton Live. Ableton intercepts the CC 96/97 data-increment bytes before they reach the plugin, so the plugin only ever sees the address bytes (CC 98/99) and can never fire an event. This is a host-side routing limitation; nothing in INTERSECT can route around it. If you need controller-driven slice editing inside Ableton, route MIDI through a wrapper like Max for Live that can translate NRPN to plain CCs before the plugin sees it — but INTERSECT itself doesn't accept absolute CC editing.

**REAPER / Bitwig:** Route a MIDI track to the plugin instance as normal. No additional configuration is needed; all CC messages are forwarded to the plugin.

## If nothing happens, check…

- **NRPN editing is on** in **SET → MIDI**.
- **The MIDI channel matches.** If you set a specific channel in INTERSECT and the controller sends on a different one, edits are ignored. Try `0` (omni) while debugging.
- **`FM` is on** — without it, the boundary edits target whichever slice is selected, which may not be the one you expect.
- **A slice is selected** — zoom works without a slice, but slice start/end don't.
- **Your controller is actually sending CC 96/97.** Use a MIDI monitor (REAPER's MIDI Inspector, MIDI-OX, etc.) to confirm. If you see CC 6 instead, the controller is in absolute mode — switch to relative/NRPN mode.
- **The DAW isn't eating the CCs.** Some DAWs strip NRPN before delivering MIDI to plugins; see the Ableton note above.

For broader problems, see [Troubleshooting → MIDI]({{ site.baseurl }}{% link troubleshooting.md %}#midi).
