---
title: Build from source
nav_order: 14
description: "Per-OS toolchain setup, the clone+build steps, the optional Podman path for glibc-compatible Linux builds, and how to troubleshoot the build."
---

# Build from source

## Prerequisites

- CMake `3.22+`
- C++20 compiler
- Git

## Platform setup

Pick your platform below to install the required toolchain and libraries, then follow the shared **Clone and build** steps.

<details markdown="block">
<summary><strong>Windows</strong></summary>

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/vs/community/) (Community edition is free).
   During installation, select the **"Desktop development with C++"** workload.
2. Install [CMake](https://cmake.org/download/) (add to PATH during install) and [Git](https://git-scm.com/download/win).

</details>

<details markdown="block">
<summary><strong>macOS</strong></summary>

1. Install Xcode Command Line Tools:
   ```bash
   xcode-select --install
   ```
2. Install CMake via [Homebrew](https://brew.sh/):
   ```bash
   brew install cmake
   ```

</details>

<details markdown="block">
<summary><strong>Linux — Debian / Ubuntu</strong></summary>

```bash
sudo apt update
sudo apt install -y build-essential cmake git libasound2-dev libfreetype-dev \
  libx11-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libwebkit2gtk-4.1-dev libcurl4-openssl-dev
```

</details>

<details markdown="block">
<summary><strong>Linux — Fedora</strong></summary>

```bash
sudo dnf install -y gcc-c++ cmake git alsa-lib-devel freetype-devel \
  libX11-devel libXrandr-devel libXcursor-devel libXinerama-devel \
  webkit2gtk4.1-devel libcurl-devel
```

</details>

<details markdown="block">
<summary><strong>Linux — Arch</strong></summary>

```bash
sudo pacman -S --needed base-devel cmake git alsa-lib freetype2 \
  libx11 libxrandr libxcursor libxinerama webkit2gtk-4.1 curl
```

</details>

## Clone and build

```bash
git clone --recursive https://github.com/tucktuckg00se/INTERSECT.git
cd INTERSECT
cmake -B build
cmake --build build --config Release
```

### Build only the VST3

If you don't need the Standalone or AU outputs, target just the VST3 to cut build time roughly in half:

```bash
cmake --build build --config Release --target Intersect_VST3 -j
```

### Run cppcheck before building (recommended for contributors)

```bash
cppcheck --enable=warning,performance --suppress=missingInclude src/
```

<details markdown="block">
<summary><strong>Linux: building inside Ubuntu 22.04 Podman (matches release glibc)</strong></summary>

Release Linux builds use a pinned Ubuntu 22.04 Podman image so the resulting `.vst3` works on any glibc 2.35+ system. Native Arch (or other modern-glibc distro) builds will link against your local glibc and won't run on the release baseline.

If you want your local build to be glibc-compatible with the release:

```bash
podman build -t intersect-ubuntu2204-build -f .containers/Containerfile.ubuntu2204-build .

podman run --rm \
  -v "$PWD:$PWD:Z" \
  -w "$PWD" \
  intersect-ubuntu2204-build \
  bash -lc 'cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release --target Intersect_VST3 -j2'
```

Mount the repo at the same absolute path inside Podman so CMake caches in `build/` remain valid.

After the build, sanity-check that the binary doesn't pull in newer-than-Ubuntu-22.04 glibc symbols:

```bash
objdump -T build/Intersect_artefacts/Release/VST3/INTERSECT.vst3/Contents/x86_64-linux/INTERSECT.so | rg 'GLIBC_2\.(4[0-9]|3[6-9])'
```

The command should print no matches.

</details>

## ONNX Runtime

The build does not bundle any ONNX Runtime shared library. Only the ORT C++ headers are pulled in at configure time (via CMake `FetchContent`) so the plugin compiles against the published ORT API. The shared library itself is downloaded at runtime via **SET → Stem Separation → ONNX Runtime** — see [Stem separation setup]({{ site.baseurl }}{% link installation.md %}#stem-separation-setup). This keeps each plugin zip small and lets users pick their GPU (CPU / CUDA / MIGraphX / DirectML / CoreML) without needing a separate build.

## Build outputs

- VST3: `build/Intersect_artefacts/Release/VST3/INTERSECT.vst3`
- Standalone:
  - Windows: `build/Intersect_artefacts/Release/Standalone/INTERSECT.exe`
  - Linux: `build/Intersect_artefacts/Release/Standalone/INTERSECT`
  - macOS: `build/Intersect_artefacts/Release/Standalone/INTERSECT.app`
- AU (macOS): `build/Intersect_artefacts/Release/AU/INTERSECT.component`

## Release workflow (repo maintainers)

Pushing a tag matching `v*` triggers the GitHub Actions release workflow, which builds and packages six small plugin zips:

- Windows x64
- Windows ARM64 (built natively on GitHub's `windows-11-arm` runner)
- Linux x64
- Linux ARM64 (built natively on GitHub's `ubuntu-22.04-arm` runner, same glibc 2.35 baseline as x64)
- macOS arm64
- macOS x64 (no stem separation — ONNX Runtime 1.24 dropped x86_64 macOS)

The workflow can also be started by hand (**Actions → Release → Run workflow**) to build every platform from a branch without publishing a release. To build ARM64 on Windows yourself, configure with `cmake -B build -A ARM64` from a Visual Studio install that has the ARM64 build tools.

ONNX Runtime bundles are published separately from the [intersect-ort-providers](https://github.com/tucktuckg00se/intersect-ort-providers) repo and downloaded on demand from the plugin.

## Dependencies

- [JUCE](https://github.com/juce-framework/JUCE) (git submodule)
- [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT)
- [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) (dependency of Signalsmith Stretch)
- [Bungee](https://github.com/bungee-audio-stretch/bungee) (MPL-2.0)
- [ONNX Runtime](https://onnxruntime.ai/) (MIT) — headers only at build time; shared library downloaded on demand

## Troubleshooting build errors

- **"Missing submodule" / "JUCE not found" / "signalsmith-stretch missing":** You cloned without `--recursive`, or new submodules were added since your last pull. Run `git submodule update --init --recursive`.
- **Linux: missing X11/ALSA/WebKit headers:** Install the dev packages from the platform setup section above. The package names differ by distro.
- **Windows: "Cannot find compiler 'cl.exe'":** Open a "Developer Command Prompt for VS 2022" before running CMake, or pass `-G "Visual Studio 17 2022"` so CMake locates the VS toolchain itself.
- **macOS: `xcrun: error: invalid active developer path`:** Run `xcode-select --install` to install the Command Line Tools.
- **CMake configure step downloads ONNX Runtime headers slowly:** This is a one-time `FetchContent` download per build directory. Subsequent configures reuse the cached copy.
- **Build succeeds but the plugin won't load in your DAW:** rebuild after a `cmake --build build --target clean` if you switched compiler versions or changed CMake options between builds.
