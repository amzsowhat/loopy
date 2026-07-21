# Sound VST Project

A cloud-first monorepo for multiple independent VST3 audio tools. Each plug-in has its own product
directory, documentation, source, tests, and build target, while repository-level configuration
keeps Codex sessions and CI consistent across machines.

## Products

| Product | Directory | Status |
| --- | --- | --- |
| P2 Loop Surgeon | [`plugins/loop-surgeon`](plugins/loop-surgeon) | Functional capture/loop demo |

## Demo capabilities

Loop Surgeon builds as both a VST3 effect and a Standalone application. One capture automatically
searches around the requested duration for the strongest loop boundary using level, slope,
spectrum, phase, and stereo-continuity measurements. The selected audio and parameters are stored
in the DAW project, and a click-reducing seam crossfade is applied during playback.

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

## Build on macOS for Reaper

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
