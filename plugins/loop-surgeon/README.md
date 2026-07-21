# P2 Loop Surgeon

Loop Surgeon is a VST3 effect for turning one capture into a usable loop without manually placing
start and end markers. It searches around the requested duration, compares candidate boundaries,
and publishes the best result after background analysis. The repository also builds a Standalone
target for quick testing without a plug-in host.

## Current workflow

1. Insert the effect on a mono or stereo track, or open the Standalone target and choose an input.
2. Set the approximate **Capture Length**, **Crossfade**, and **Loop Mix**.
3. Press **Capture Input** and play the source material.
4. The plug-in captures a small search margin and automatically chooses the loop start and end.
5. The resulting audio is stored with the DAW project and loops automatically.
6. Press **Clear Loop** to return to dry monitoring.

The score combines boundary level, slope, an eight-band spectral signature, normalized phase
correlation, and stereo correlation continuity. Analysis runs outside the real-time audio thread.
It is an explainable heuristic, not a trained perceptual model or a guarantee that every source can
form a musically convincing loop.

## Parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `loopLength` | 0.25-16 s | 4 s | Approximate target duration |
| `syncToHost` | off/on | on | Arm capture for the next host bar |
| `bars` | 1/2/4/8 | 1 | Musical loop size when host sync is on |
| `crossfadeMs` | 1-250 ms | 25 ms | End-to-start crossfade |
| `mix` | 0-100% | 100% | Dry/loop balance after capture |

## State and memory

- Parameters and gzip-compressed 32-bit float loop audio are embedded in the host project state.
- The capture workspace is allocated once in `prepareToPlay`; capture and clear never allocate or
  erase the full buffer on the audio thread.
- The published loop buffer is allocated to the selected length rather than the 16-second maximum.

## Known limitations

- Capture is not aligned to the host bar/beat grid yet.
- The spectral signature uses eight deterministic analysis bands, not a full perceptual model.
- There is no file import/export or waveform editor yet.
- State restore uses linear sample-rate conversion when a project changes sample rate.
- macOS/Reaper is configured for a universal build but awaits physical-machine verification.
