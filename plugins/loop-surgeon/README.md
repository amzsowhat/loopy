# P2 Loop Surgeon

Loop Surgeon is a VST3 effect for capturing audio from a DAW, auditioning it as a loop, and reducing
the discontinuity at the loop boundary. The repository also builds a Standalone target for quick
testing without a plug-in host.

## Current demo workflow

1. Insert the effect on a mono or stereo track, or open the Standalone target and choose an input.
2. Set **Capture Length**, **Crossfade**, and **Loop Mix**.
3. Press **Capture Input** and play the source material.
4. After the capture finishes, the region loops automatically.
5. Press **Clear Loop** to return to dry monitoring.

The seam-quality score is a lightweight time-domain diagnostic, not a claim that a loop is
perceptually seamless. Production scoring is planned in `docs/ROADMAP.md`.

## Parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `loopLength` | 0.25–16 s | 4 s | Target capture duration |
| `crossfadeMs` | 1–250 ms | 25 ms | End-to-start crossfade |
| `mix` | 0–100% | 100% | Dry/loop balance after capture |

## Known limitations

- Captured audio is not yet restored with the host project.
- The loop start is fixed at the beginning of the capture.
- Seam scoring is time-domain only; there is no spectral or phase analysis yet.
- There is no file export in the audio-thread demo.

