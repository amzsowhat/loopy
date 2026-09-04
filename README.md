# loopy

**Turn recorded sound into clean, exact-length loops.**

loopy is a JUCE audio plug-in for sound designers working with ambience, machinery,
weather, room tone and other continuous source material. Load a recording, select the
useful range, choose an output length and generate a loop that is ready to audition,
export or drag into a DAW.

It provides two workflows in one VST3:

- **Rotate & Repair** finds a loopable section at the requested length, moves the
  boundary to a safer point and repairs the internal seam. Use it when the result is
  equal to or shorter than the selected source.
- **Texture Loop** constructs a longer, evolving loop from phase-compatible regions of
  the selected recording. Use it when a short source needs to become a longer bed
  without time-stretching, reversing or introducing synthetic material.

## What it does

- Produces an exact user-defined duration.
- Searches candidate boundaries using waveform, phase, level, spectrum, transient and
  stereo-continuity measurements.
- Keeps stereo traversal linked.
- Supports WAV, AIFF, FLAC and OGG input.
- Exports 24-bit WAV and supports drag-to-DAW delivery.
- Stores the generated result with the DAW project state.
- Applies DC/non-finite cleanup and a `-1 dBTP` true-peak ceiling.

## Basic workflow

1. Load a recording or capture the plug-in input.
2. Set **Source In** and **Source Out** around the material you want to use.
3. Choose **Rotate & Repair** or **Texture Loop**.
4. Set the required output length and adjust the mode controls.
5. Generate and audition the result for several complete repetitions.
6. Save a WAV or drag the result into the DAW.

### Example: 10-second source to a 5-second loop

In **Rotate & Repair**, set **Final Length** to `5.0 s`. loopy does not simply
take the first five seconds: it searches the selected source for suitable exact-length
windows, ranks their boundaries, rotates the chosen material around a safe internal cut
and crossfades the moved seam.

This removes discontinuities more reliably than a fixed crop, but no algorithm can make
every recording perceptually seamless. One-shot impacts, speech, strong pass-bys and
continuous rises or falls may still reveal repetition.

## Controls

| Rotate & Repair | Purpose |
| --- | --- |
| Final Length | Selection length or an exact shorter duration |
| Seam | Maximum repair overlap |
| Audition | Dry/generated monitoring balance |
| Loop Start / Join Position | Move the repaired loop boundary |
| Options A-C | Alternative analysed candidates |

| Texture Loop | Purpose |
| --- | --- |
| Length | Exact output duration |
| Stability | Reduce broad source-envelope movement |
| Crush | Reduce smaller repeated amplitude pulses |
| Transform | Depart further from the original timeline |
| Flow / Drift / Fracture | Choose the traversal scale |
| Patina / Bloom / Fray | Optional downstream character processing |

## Build

Requirements:

- CMake 3.22 or newer
- Visual Studio 2022 with the C++ desktop workload, or Xcode on macOS
- Git access during first configuration so CMake can fetch JUCE 8.0.13

Windows Debug build:

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

The resulting Windows plug-in is under:

```text
build/vs2022/plugin/LoopSurgeon_artefacts/Debug/VST3/loopy.vst3
```

Copy the complete `loopy.vst3` bundle to:

```text
C:\Program Files\Common Files\VST3\
```

Then clear the VST cache or rescan in your DAW. See
[`docs/WINDOWS_REAPER_TEST.md`](docs/WINDOWS_REAPER_TEST.md) for the current
host-check procedure.

## Project status

The deterministic engine and state tests pass on the current Windows build. A successful
build does not establish perceptual loop quality across all source material or full host
compatibility. Real recordings, repeated listening and target-DAW testing remain required
before release.

The macOS target is configured but has not been built or signed on this Windows development
machine.

## Repository

- `plugin/Source/` — DSP, analysis, state and JUCE interface
- `plugin/Tests/` — deterministic engine tests
- `docs/` — algorithms, architecture and host-test guidance
- `plugin/Assets/` — bundled Space Grotesk fonts and license

## License

No project-wide source-code license has been granted yet. JUCE is dual-licensed; confirm
the applicable JUCE terms before distributing a proprietary build. Space Grotesk is
included under the SIL Open Font License in `plugin/Assets/SpaceGrotesk-OFL.txt`.
