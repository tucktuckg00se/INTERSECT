---
title: Sample groups
nav_order: 6.2
description: "Slice a song's stems once and play them together, with a mute, level and output per stem for the whole group or per slice."
---

# Sample groups

A **group** is two or more samples of the same length that share one set of slices, like the stems of one song. Each slice plays every sample in the group together on one key. You cut the song once and can still rebalance or isolate its parts, for the whole group or slice by slice.

This page walks through making a group from stems, slicing it, mixing it and giving single slices their own mix. For the rules in short, see [Concepts → Sample groups]({{ site.baseurl }}{% link workflow-basics.md %}#sample-groups).

## 1. Make a group

**From stem separation.** In the stem view, set `MODE` to `Group` and press `START`. When the stems arrive they become a group named `STEMS · <source>`, and it starts with a copy of the source sample's slices. The source sample stays as it was, so nothing plays twice. See [Stem separation]({{ site.baseurl }}{% link stem-separation.md %}).

![A sliced song next to a group of its four stems, which starts with a copy of the song's eight slices]({{ site.baseurl }}/assets/images/groups-from-stems.png)
*The song on the left, and its stems as a group on the right, with a copy of the song's slices (9–16).*

**From samples you already have.** Right-click a sample in the sample lane and choose `Group with...`, or point at a sample in the waveform and press `+ GROUP`. The list offers every sample of the same length (within 10 ms, since stems can come back a frame longer than their source); the others are greyed with the reason. When samples join a group, their own slices are removed, and INTERSECT asks first if they have any. The group keeps the slices of the sample you started from.

To add more later, press `+ ADD SAMPLE` under the lanes, or right-click the group and choose `Add sample...`.

![The Add to group picker listing song.wav, the one sample of the same length that isn't grouped yet]({{ site.baseurl }}/assets/images/groups-add-sample.png)
*Adding to a group: only samples of the same length can join.*

## 2. The lanes

The waveform shows a group as one stretch with a lane per sample, numbered from the top in the group's colours. Each lane has a small header:

- The **number** is the sample's place in the group. Numbers follow the lane order, so they change when a sample leaves.
- **`M`** mutes that sample for the whole group.
- **Point at a header** and it opens: the sample's name (a stem shows its stem name), a level slider (double-click it for 0 dB), `x` to take the sample out of the group, and `...` for its menu.

The more samples, the shorter the lanes, and the headers adapt: at full height you get everything above, then just the name, `M` and the menu, and at 16 samples a thin strip whose name appears when you point at it (right-click it for the menu). A group holds up to 16 samples.

![Four stem lanes; the second lane's header is open, showing BASS, its level at 0.0 dB, a remove button and a menu]({{ site.baseurl }}/assets/images/groups-lanes.png)
*Four lanes; pointing at lane 2 opens its header.*

## 3. Slice once

Slice the group as you would any sample: draw with `ADD`, chop with `LAZY`, or split with `AUTO`. Every slice spans all the lanes and plays every sample on its key.

The top sample owns the group's slices, and its sample settings are the group's settings: while a group is selected the `SAMPLE` tab reads `GROUP`. `AUTO`, zero-crossing snap (`ZX`), `LAZY` and Shift+click preview all listen to the group's mix, not just the top lane.

![A slice of the group selected: the SLICE 11 tab, MEMBERS showing all four stems and "group mix", and an M at the top-left of each lane inside the slice]({{ site.baseurl }}/assets/images/groups-slice.png)
*A group slice selected: the tab reads GROUP, and MEMBERS shows the stems it plays.*

## 4. Mix the group

`MIX` on the group's block in the sample lane lists every sample in the group with its `M`, its level and its output: `Slice OUT` follows the slice's own `OUT`, or pick `Out 1`–`Out 16` to send a stem to its own track in your DAW. A sample on its own output adds a voice for each note. `x` takes a sample out of the group.

Changes to mutes and levels apply from the next note, like the filter settings.

![The MIX popover with DRUMS, BASS, OTHER and VOCALS, each with an M button, a level slider at 0.0 dB, Slice OUT and a remove button]({{ site.baseurl }}/assets/images/groups-mix.png)
*MIX: mute, level and output for each stem.*

## 5. Give a slice its own members

A slice follows the group mix until you give it its own choice of samples. Use either:

- the **`M`s inside the selected slice**, at the top-left of each lane. They appear when the slice is wide enough and the lanes tall enough; zoom in if you don't see them.
- **`MEMBERS`** on the `SLICE` tab's bottom row. It shows the numbers of the samples the slice plays, then `group mix` or `own`, and works for any slice and any number of samples.

A slice with its own members plays only those, at the group's levels. Its `M`s light up, the lanes it doesn't play are hatched, its block in the slice lane gets an orange corner, and `MEMBERS` turns orange.

![Slice 11 playing only bass and vocals: the drums and other lanes are hatched inside the slice, its M buttons are lit, the slice block has an orange corner and MEMBERS reads "own"]({{ site.baseurl }}/assets/images/groups-own-members.png)
*Slice 11 with its own members: drums and keys muted in this slice only.*

In the `MEMBERS` list, untick `Group mix` to give the slice its own members (starting from what it plays now), or tick it to go back to the group mix. `UNMUTE ALL` plays every sample in that slice.

![The MEMBERS list for slice 11: Group mix unticked, UNMUTE ALL, and DRUMS and OTHER muted]({{ site.baseurl }}/assets/images/groups-members-list.png)
*The MEMBERS list.*

How the two kinds of mute work together:

- A lane's `M` changes only the slices that follow the group mix. Muting a lane that slices with their own members still play shows a note in the header.
- A sample added to the group plays only in slices that follow the group mix.
- Taking a sample out of the group takes it out of every slice's list; a slice left with an empty list goes back to the group mix.

## 6. Menus

Right-click the group's block in the sample lane for `Add sample...`, `Mix...`, `Remove from group`, `Duplicate as separate sample`, `Ungroup` and `Remove group...`. Right-click a single sample for `Group with...`, `Add to group`, `Duplicate sample` and `Remove...`. Right-click a lane header (or click its `...`) for `Mute`, `Mix...`, `Output`, `Duplicate as separate sample` and `Remove from group`.

Items with `›` open their list in the same box; click the heading or press `Left` to go back. `Up`, `Down` and `Return` work too, and `Esc` or a click outside closes the menu.

![The group's right-click menu: Add sample, Mix, Remove from group, Duplicate as separate sample, Ungroup and Remove group]({{ site.baseurl }}/assets/images/groups-menu.png)
*Right-click the group's block for its menu.*

## 7. Export

`AUDIO` on the group's block gives you a WAV of the group's mix (the unmuted samples at their levels): drag it into your DAW or a folder, or click it to save a copy. `MIDI` gives a clip of the group's slices, like any sample's.

## 8. Leaving a group, copies and limits

- **Take a sample out** with `x` on its header, in `MIX`, or `Remove from group`. It becomes a standalone sample with a copy of the group's slices, if there's room (128 slices in all). If it was the top sample, the next one takes over the slices and the group's settings.
- **Ungroup** does that for every sample. If there isn't room for all the copies, the top sample keeps the slices and INTERSECT says so.
- A group left with **one sample** ends by itself; that sample keeps the slices.
- **Remove group...** removes every sample in the group, after asking. One undo brings it all back.
- **Duplicate sample** adds a standalone copy with the same settings and a copy of the slices on new keys. For a sample in a group it takes the group's slices and settings, so a stem can be in a group and on its own too.
- **Limits:** 16 samples per group, 32 groups, and 128 slices in the whole session.
- **Missing files:** a sample whose file is missing plays silence and the rest of the group still plays; relink it with the usual **MISSING SAMPLE, RELINK REQUIRED** prompt in the header. The group's slices pause only if every sample is missing.
- **Older versions** of INTERSECT open a project with groups with the samples separate, and the group's slices play only the top sample.
