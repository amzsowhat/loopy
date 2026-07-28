# Sound VST Project

A cloud-first monorepo for multiple independent VST3 audio tools. Each plug-in has its own product
directory, documentation, source, tests, and build target, while repository-level configuration
keeps Codex sessions and CI consistent across machines.

## Products

| Product | Directory | Status |
| --- | --- | --- |
| P2 Loop Surgeon | [`plugins/loop-surgeon`](plugins/loop-surgeon) | One-shot texture and seamless-loop alpha |

## Demo capabilities

Loop Surgeon builds as both a VST3 effect and a Standalone application. Drop in source audio, limit
the material with Source In/Out, then choose Auto, Evolving Texture, or Seam Loop. Texture mode
recombines acoustically compatible long grains into a 4–60 second circular result with three seeded
variations; Seam Loop retains the explainable period and boundary-repair path for periodic material.
The selected result is stored in the DAW project and exports as a 24-bit WAV.

## Build on Windows

Requirements:

- CMake 3.22+
- Visual Studio 2022 with Desktop development with C++
- Git and internet access for the pinned JUCE dependency

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

Release builds use `vs2022-release` and `build-release`.

## Download for Windows / Reaper

The 0.5 Alpha texture-engine source is on `main`, but the Windows package is not listed as
downloadable until the `v0.5.0-alpha-windows-ready` CI marker exists. Earlier binaries do not contain
Evolving Texture, the three processing modes, or immediate Source/Generated audition.

Unzip the package and copy the complete `Loop Surgeon.vst3` folder to:

```text
C:\Program Files\Common Files\VST3\
```

Open 64-bit Reaper and use **Options > Preferences > Plug-ins > VST > Clear cache/re-scan**.
See [`plugins/loop-surgeon/WINDOWS_REAPER_TEST.md`](plugins/loop-surgeon/WINDOWS_REAPER_TEST.md)
for the smoke-test checklist.

## Build on macOS for Reaper

### Download the M-series Mac build

The Apple Silicon package is not listed as downloadable until the
`v0.5.0-alpha-macos-ready` CI marker exists. Earlier binaries are not a valid test of the current
workflow or algorithm.

Unzip it and copy `Loop Surgeon.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. If macOS blocks
the private ad-hoc-signed test build, run:

```bash
xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Loop Surgeon.vst3"
```

Open the native Apple Silicon build of Reaper and use **Preferences > Plug-ins > VST > Re-scan**.

### Build from source

Install Xcode command-line tools, CMake, and Git, then run:

```bash
cmake --preset macos-debug
cmake --build --preset build-macos-debug
ctest --preset test-macos-debug
```

The macOS VST3 targets Apple Silicon (`arm64`, including M1/M2/M3/M4). Copy `Loop Surgeon.vst3` to
`~/Library/Audio/Plug-Ins/VST3/`, open Reaper, and run **Preferences > Plug-ins > VST > Re-scan**.
The macOS target is configured but must still be verified on physical Mac/Reaper hardware.
See [`docs/MAC_REAPER_TEST.md`](docs/MAC_REAPER_TEST.md) for the full test checklist and
[`docs/LOOP_ALGORITHM.md`](docs/LOOP_ALGORITHM.md) for the automatic-boundary algorithm.

## Repository layout

```text
plugins/                 Independent product directories
shared/                  Shared code only when reused by multiple products
docs/                    Cross-product architecture and roadmap
.github/workflows/       Reproducible cloud builds
AGENTS.md                Handoff context for future Codex sessions
```

## Licensing note

The project uses JUCE. JUCE is dual-licensed, and proprietary distribution may require a commercial
JUCE licence. Review the current JUCE terms before selling or distributing binaries.
