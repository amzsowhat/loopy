# loopy

[English](README.md) | [简体中文](README.zh-CN.md)

**Build seamless, exact-length loops from recorded sound.**

loopy is a JUCE audio plug-in for sound designers working with ambience, machinery,
weather, room tone and other continuous recordings. Load or record audio, isolate the
useful material, set the required duration, then generate a loop for audition, WAV export
or drag-and-drop into a DAW.

## Two modes

| | LOOP | TEXTURE |
| --- | --- | --- |
| Intent | Automate the conventional sound-designer loop-authoring workflow | Construct a continuous texture from the selected recording |
| Source relationship | Keeps one continuous forward-moving cycle | Reorganises only source-derived, phase-compatible regions while preserving material identity |
| Output length | Any exact duration: shorter, equal or longer than the source selection | Any exact duration |
| Longer output | Repeats a prepared cycle on phase-aligned boundaries | Continues an evolving, deterministic traversal |
| Primary shaping | Seam placement and repair | Macro envelope, local dynamics, temporal traversal and optional character |
| Best suited to | Material whose original timeline and motion should remain intact | Material that should become a steadier texture without turning into synthetic or generic noise |

`LOOP` does not define itself by shortening. For a shorter target it searches the
selection for a strong exact-length cycle. For a longer target it first derives and
prepares a viable loop, then repeats complete cycles so the final boundary returns to the
same phase. It covers the operations a sound designer normally performs when authoring a
loop: choosing usable material, setting boundaries, creating a continuous join and fitting
the result to the required duration.

`TEXTURE` is a separate source-driven construction mode, not merely the long-output mode
and not only an ADSR flattener. It can reduce macro envelope and pass-by motion, control
smaller repeated dynamics, and depart from the literal source timeline by joining
phase-compatible regions on a circular timeline. The selected recording remains the only
sound material; no oscillator, synthetic-noise bed, pitch shift, stretch or reverse path
replaces it.

## Features

- Exact user-defined duration in both modes.
- Boundary analysis using waveform, phase, level, spectrum, transient and stereo
  continuity measurements.
- Linked stereo traversal and processing.
- WAV, AIFF, FLAC and OGG input.
- 24-bit WAV export and drag-to-DAW delivery.
- Generated audio stored with the DAW project state.
- DC/non-finite cleanup and a `-1 dBTP` true-peak ceiling.

## Workflow

1. Load a recording or capture the plug-in input.
2. Set **Source In** and **Source Out** around useful material.
3. Choose **LOOP** or **TEXTURE**.
4. Set the required output length and adjust the mode controls.
5. Generate and audition several complete repetitions.
6. Save a WAV or drag the result into the DAW.

## Controls

| LOOP | Purpose |
| --- | --- |
| Final Length | Use the selection or enter any exact output duration |
| Seam | Set the maximum boundary crossfade |
| Audition | Blend source and generated monitoring |
| Loop Start / Join Position | Move the loop boundary |
| Options A-C | Choose among analysed cycle candidates |

| TEXTURE | Purpose |
| --- | --- |
| Length | Set the exact output duration |
| Stability | Reduce broad source-envelope movement |
| Crush | Reduce smaller repeated amplitude pulses |
| Transform | Control departure from the original timeline |
| Flow / Drift / Fracture | Choose traversal scale and continuity |
| Patina / Bloom / Fray | Apply optional downstream character |

## Build

Requirements: CMake 3.22+, Visual Studio 2022 with the C++ desktop workload (or
Xcode on macOS), and Git access during first configuration so CMake can fetch JUCE
8.0.13.

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

The Windows Debug VST3 is generated at:

```text
build/vs2022/plugin/LoopSurgeon_artefacts/Debug/VST3/loopy.vst3
```

Copy the complete `loopy.vst3` bundle to `C:\Program Files\Common Files\VST3\`, then
rescan plug-ins in the DAW. See [`docs/WINDOWS_REAPER_TEST.md`](docs/WINDOWS_REAPER_TEST.md).

## Verification boundary

Deterministic engine and state tests verify exact duration, repeatable generation and
numeric safety. They do not establish perceptual seamlessness for every recording or
host compatibility. Real-source listening and target-DAW acceptance remain necessary.

The macOS target is configured but has not been built or signed on this Windows machine.

## Repository

- `plugin/Source/` — analysis, DSP, state and JUCE interface
- `plugin/Tests/` — deterministic engine tests
- `docs/` — algorithms, architecture and host-test guidance
- `plugin/Assets/` — bundled Space Grotesk fonts and license

## License

No project-wide source-code license has been granted. JUCE is dual-licensed; confirm the
applicable JUCE terms before distribution. Space Grotesk is included under the SIL Open
Font License in `plugin/Assets/SpaceGrotesk-OFL.txt`.
