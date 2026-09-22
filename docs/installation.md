---
title: Installation
nav_order: 2
description: "Install INTERSECT on Windows, macOS, or Linux (x64 and ARM64) — plugin paths, macOS quarantine, ONNX Runtime bundles for stem separation, GPU requirements, and uninstall."
---

# Installation

Download the latest release zip from [Releases](https://github.com/tucktuckg00se/INTERSECT/releases), then place plugin files in your system plugin folders.

## Release package contents

| Platform | Minimum OS | Stem separation | Included binaries |
| --- | --- | --- | --- |
| Windows x64 | Windows 10 | On-demand ONNX Runtime bundle (CPU or DirectML GPU) | `INTERSECT.vst3`, `INTERSECT.exe` |
| Windows ARM64 | Windows 11 on Arm | On-demand ONNX Runtime bundle (CPU) | `INTERSECT.vst3`, `INTERSECT.exe` |
| Linux x64 | Ubuntu 22.04+ (glibc 2.35+) | On-demand ONNX Runtime bundle (CPU / NVIDIA CUDA 12 / NVIDIA CUDA 13 / AMD MIGraphX) | `INTERSECT.vst3`, `INTERSECT` (standalone) |
| Linux ARM64 (aarch64) | Ubuntu 22.04+ or another aarch64 distro with glibc 2.35+ | On-demand ONNX Runtime bundle (CPU) | `INTERSECT.vst3`, `INTERSECT` (standalone) |
| macOS arm64 (Apple Silicon) | macOS 10.13 (High Sierra) | On-demand ONNX Runtime bundle (CoreML) | `INTERSECT.vst3`, `INTERSECT.component`, `INTERSECT.app` |
| macOS x64 (Intel) | macOS 10.13 (High Sierra) | Not available — ONNX Runtime 1.24 dropped x86_64 macOS support | `INTERSECT.vst3`, `INTERSECT.component`, `INTERSECT.app` |

Pick the zip for your operating system **and** processor: `x64` for Intel/AMD, `arm64` for ARM (Apple Silicon, Snapdragon, Raspberry Pi and other aarch64 boards). An ARM64 plugin only loads in an ARM64-native DAW; an x64 DAW running under emulation needs the x64 plugin.

Plugin zips are small (single-digit MB); the ONNX Runtime required for stem separation is downloaded on demand from inside the plugin — see [Stem separation setup](#stem-separation-setup).

## Plugin install paths

| Format | Windows | macOS | Linux |
| --- | --- | --- | --- |
| VST3 | `C:\Program Files\Common Files\VST3\` | `~/Library/Audio/Plug-Ins/VST3/` | `~/.vst3/` |
| AU | n/a | `~/Library/Audio/Plug-Ins/Components/` | n/a |

After copying files, rescan plugins in your DAW.

## macOS — clear quarantine on first launch

INTERSECT release builds are unsigned, so macOS may report on first launch that INTERSECT is "damaged" or "cannot be opened". Clear the quarantine flags from a Terminal:

```bash
xattr -cr ~/Library/Audio/Plug-Ins/VST3/INTERSECT.vst3
xattr -cr ~/Library/Audio/Plug-Ins/Components/INTERSECT.component
xattr -cr /Applications/INTERSECT.app
```

Then re-launch your DAW (or the standalone app) and rescan plugins.

## Stem separation setup

Stem separation requires two on-demand downloads performed from inside the plugin. Both live under **SET → Stem Separation** in the header bar.

1. **ONNX Runtime bundle** — open **SET → Stem Separation → ONNX Runtime** and pick the bundle that matches your platform and (optionally) your GPU. Only one bundle is active at a time; installing a different one switches which runtime the plugin loads. Restart INTERSECT after the install completes so the new runtime is picked up.
2. **Stem model** — open **SET → Stem Separation → Download Models** and pick a BS-RoFormer model.

After both are installed, open a sample's `STEMS` button to export.

## GPU runtime requirements (per ONNX Runtime bundle)

- **Windows — DirectML** — no extra installs required. Works with any GPU vendor (NVIDIA, AMD, Intel) on Windows 10+ with a recent DirectX 12-capable driver.
- **macOS arm64 — CoreML** — no extra installs required. Uses the built-in Apple Neural Engine / GPU.
- **macOS x64 (Intel Mac)** — stem separation is not available. ONNX Runtime 1.24 dropped x86_64 macOS support.
- **Linux — NVIDIA CUDA 12 or CUDA 13** — pick the bundle that matches the CUDA runtime installed on your system. Requirements:
  - NVIDIA proprietary driver
  - CUDA runtime — `libcudart.so.12` for the CUDA 12 bundle, `libcudart.so.13` for the CUDA 13 bundle
  - cuDNN 9 (`libcudnn.so.9`)

  Install on Arch: `sudo pacman -S cuda cudnn`
  Install on Ubuntu: `sudo apt install nvidia-cuda-toolkit libcudnn9-cuda-12` (adjust the cuDNN package name to match your CUDA version)

  Flatpak DAW hosts, such as the Flatpak build of Bitwig Studio, may need extra library-path configuration even when GPU access is enabled in Flatseal. If the standalone app can use CUDA but the Flatpak-hosted plugin reports `OrtSessionOptionsAppendExecutionProvider_Cuda: Failed to load shared library`, add these environment variables to the DAW in Flatseal. CUDA install paths vary by distro — use the one that matches yours:

  ```text
  # Arch (CUDA installed at /opt/cuda)
  LD_LIBRARY_PATH=/opt/cuda/lib64:/usr/lib/x86_64-linux-gnu/GL/lib
  LD_PRELOAD=/run/host/usr/lib/libcudnn.so.9
  ```

  ```text
  # Ubuntu/Debian (CUDA installed at /usr/local/cuda)
  LD_LIBRARY_PATH=/usr/local/cuda/lib64:/usr/lib/x86_64-linux-gnu/GL/lib
  LD_PRELOAD=/run/host/usr/lib/libcudnn.so.9
  ```

- **Linux — AMD MIGraphX** — requires ROCm installed. See [AMD's ROCm install guide](https://rocm.docs.amd.com/projects/install-on-linux/en/latest/). Experimental.
- **Linux — CPU** — no extra installs required.
- **Windows ARM64 and Linux ARM64** — the CPU bundle is the only option; no extra installs required.

If a GPU runtime is missing or fails to load at export time, INTERSECT shows the error in the header status bar instead of silently failing; you can then switch `DEVICE` to CPU or install the missing runtime.

## Verify your install

1. Open your DAW and rescan plugins. INTERSECT should appear as an instrument plugin from "INTERSECT".
2. Add it to a track and confirm the editor opens.
3. Drag any short audio file onto the waveform — it should load and display.
4. The plugin version is shown at the bottom of the **SET** popup; use it when filing issues.

## Updating

To update to a newer release, download the latest zip and overwrite the existing files in the install paths above, then restart your DAW. Your themes, settings, and saved sessions persist across updates — they live in the per-user settings folder, not in the plugin bundle.

## Uninstall

Delete the plugin file(s) from the install paths in the table above, then rescan in your DAW.

To wipe your user data as well — themes, UI scale, NRPN settings, browser bookmarks and recent folders, saved presets, stem model folder — delete the INTERSECT settings folder for your OS:

| OS | Settings folder |
| --- | --- |
| Windows | `%APPDATA%\Roaming\INTERSECT\` |
| macOS | `~/Library/Application Support/INTERSECT/` |
| Linux | `~/.config/INTERSECT/` |

Downloaded ONNX Runtime bundles and stem models live in this folder too, so removing it also frees up that disk space. See the [Settings file]({{ site.baseurl }}{% link settings-file.md %}) page for what gets stored where.
