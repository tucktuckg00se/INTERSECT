---
title: Stem separation
nav_order: 6
description: "End-to-end walkthrough for splitting a sample into stems with INTERSECT's stem models."
---

# Stem separation

INTERSECT separates a sample into stems (drums, bass, vocals, and with some models dozens of individual instruments) using open models running through ONNX Runtime. The separation runs locally — no audio leaves your machine.

This page is a walkthrough. For runtime setup and GPU requirements, see [Installation → Stem separation setup]({{ site.baseurl }}{% link installation.md %}#stem-separation-setup).

## Models

| Model | Stems | Download | Notes |
| --- | --- | --- | --- |
| BS-RoFormer SW (jarredou) | 6: bass, drums, other, vocals, guitar, piano | 739 MB | Full-size BS-RoFormer, with separate guitar and piano stems. |
| SCNet (starrytong) | 4: drums, bass, other, vocals | 13 MB | The smallest and fastest model (8-bit weights). |
| Mini-BS-RoFormer V2.5 (HiDolen) | 4: bass, drums, other, vocals | 97 MB | Much faster than full-size models, with slightly lower quality. |
| MVSep Mega 53 (ZFTurbo / MVSep) | 53 instruments, grouped (vocals, drums, percussion, bass, guitars & plucked, keys, synth, strings, brass, woodwinds) | ~1.4 GB | Finds individual instruments. Stems overlap and don't add up to the mix. Takes about a minute to load. |

### What your computer needs

The stem view shows each model's needs under its description, and turns the RAM figure red when the whole computer has less memory than that.

| Model | RAM | CPU time for a 3-minute song | GPU |
| --- | --- | --- | --- |
| SCNet | 3 GB | ~2 min | Not needed |
| Mini-BS-RoFormer V2.5 | 5 GB | ~3 min | Optional, faster |
| BS-RoFormer SW | 6 GB | ~11 min | Recommended |
| MVSep Mega 53 | 9 GB | ~15 min | Recommended |

The RAM figures are the free memory needed while separating. CPU times were measured on a 12-core desktop CPU (Intel i7-12700K); expect longer on laptops and older machines. A GPU (`DEVICE` in the stem view, see Step 4) is much faster for the full-size models.

All four models are MIT-licensed. Each model's license and credit are shown next to it in the stem view.

## Prerequisites

1. **An ONNX Runtime bundle.** Open **SET → Stem Separation → ONNX Runtime** and pick the bundle matching your platform. Restart INTERSECT after the bundle installs.
2. **A loaded sample.** Drag any audio file onto INTERSECT's waveform.

Models can be downloaded from the stem view itself (or from **SET → Stem Separation → Download Models**). Downloads are checked against a published checksum before they are installed.

## Step 1 — Open the stem view

In the sample lane, each sample has its own `STEMS` button. Click it to open the stem view, which takes over the editing area the way the `FILES` browser does. Click `STEMS` again, press `CLOSE`, or press Esc to go back.

While a separation is running on that sample, the button reads `CANCEL`.

## Step 2 — Choose a model

The left column lists every model with its stem count and whether it's installed. Click one to see its license, credit and notes. If it isn't installed, `DOWNLOAD` fetches it; the progress shows in the list.

## Step 3 — Pick the stems

The right side lists the model's stems, grouped by instrument family. Click a stem to toggle it; `all` / `none` next to a group selects or clears that group, and `ALL` / `NONE` at the top act on every stem shown. Type in the search box to narrow the list (for example `guitar` or `drum`).

INTERSECT remembers your stem choice for each model.

## Step 4 — Device, mode and output

- `DEVICE` toggles `CPU` / `GPU` (only when a GPU-capable runtime bundle is installed). GPU is usually 10–30× faster. If the GPU can't start, INTERSECT says so in the header instead of silently falling back.
- `MODE`:
  - `Separate` — one WAV per selected stem.
  - `Combine` — the selected stems are summed into a single WAV (handy for "everything but vocals").
- `OUTPUT` — "Beside sample" writes a new folder next to the original file; `...` picks another folder, and clicking the cell goes back to "Beside sample".

## Step 5 — START

Press `START`. The view closes and the job runs in the background; you can keep working. The sample's lane block shows progress. When it finishes, the new WAVs are added to your session as new samples. Each one remembers which sample it came from and which stem it is, and starts with its source sample's settings (BPM, pitch, …).

## Cancelling

Press `CANCEL` on the sample lane to stop the job. Nothing is added to the session.

## Limitations & known issues

- **macOS Intel x64** does not support stem separation. ONNX Runtime 1.24 dropped x86_64 macOS, so Intel Macs see a "Not available" message in the stem view.
- **MVSep Mega 53** stems overlap (a piano part can appear in both `piano` and `keys`), so they don't add up to the original mix.
- **Older PCs on CPU** can take several minutes per minute of audio. This is expected.
- **Large samples + GPU** may run out of VRAM on entry-level cards. Switch to CPU if you see a GPU memory error in the header status bar.

For more specific error messages, see [Troubleshooting → Stem separation]({{ site.baseurl }}{% link troubleshooting.md %}#stem-separation).
