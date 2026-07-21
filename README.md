# Sound VST Project

A cloud-first monorepo for multiple independent VST3 audio tools. Each plug-in has its own product
directory, documentation, source, tests, and build target, while repository-level configuration
keeps Codex sessions and CI consistent across machines.

## Products

| Product | Directory | Status |
| --- | --- | --- |
| P2 Loop Surgeon | [`plugins/loop-surgeon`](plugins/loop-surgeon) | Functional capture/loop demo |

## Demo capabilities

Loop Surgeon currently builds as both a VST3 effect and a Standalone application. It can capture a
configurable amount of incoming mono/stereo audio, play the captured region as a loop, apply a
click-reducing seam crossfade, report a basic seam-quality score, and restore parameter state.

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

