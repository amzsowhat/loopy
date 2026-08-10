# P2 Loop Surgeon

Current target: **0.6.0 pre-release source**. This revision has not been built or host-validated
because automatic GitHub Actions builds are intentionally disabled while the account quota is
exhausted.

Loop Surgeon is one VST3 effect/Standalone product with two equally important processing modes.
File import is the primary workflow; DAW-input capture is secondary.

## Processing modes

- **Rotate & Repair** is for a longer ambience or environment edit that already has the desired
  content and duration but whose original head and tail do not join. It keeps the complete blue
  Source In/Out selection, chooses an internal natural cut as the new green **Loop Start**, rotates
  the two forward-running parts, and crossfades the old end/start seam after moving it inside the
  result. It never searches for or repeats a short period.
- **Texture Loop** is for turning a one-shot or changing source into a sustained layer made from
  that source's material. It builds a robust spectral model, discards the ordered ADSR/pass-by
  timeline, and synthesizes an exact-length circular stochastic result. **Flatten** controls how
  much measured macro movement remains; **Source Match** controls loudness, stereo position and
  inter-channel phase/correlation matching.

## Workflow

1. Choose a mode, then drop a WAV, AIFF, FLAC, or OGG file or click **Choose Audio...**.
2. Drag blue **Source In/Out**. This is a hard processing boundary in both modes.
3. In Rotate & Repair, set the maximum **Seam Repair** overlap and click **Repair Selected Loop**.
   Drag the green **Loop Start** to change where the completed loop begins without creating a new
   discontinuity.
4. In Texture Loop, set exact **Output Length**, **Flatten**, and **Source Match**, then click
   **Generate Texture Loop**. Two deterministic candidates are retained; **New Variation** creates
   another pair.
5. Use **Source**, **Generated**, and **Preview/Stop** for controlled A/B audition.
6. Inspect the source/output spectrum overlay, phase scope, correlation and position readouts.
7. Drag an approved 24-bit WAV directly to the DAW, or use **Save WAV...**.

The active result, source audio, source range, loop-start marker and parameters are embedded in DAW
project state. Alternate generated candidates are not embedded. Imported source and Texture Loop
output are each limited to 60 seconds. Rotate & Repair output equals the selected duration minus the
chosen overlap; the UI reports both selected and actual output length.

## Texture Loop algorithm

1. Resample only Source In/Out to the host rate and inspect up to 256 distributed active frames.
2. Reject silence and extreme hit-level outliers, then build 4096-point temporal-median Mid/Side
   magnitude models. This removes ordered rise, fall and pass-by trajectories.
3. Smooth isolated narrow peaks more strongly than broad source colour to reduce fixed electronic
   tones.
4. Draw seeded non-coherent Gaussian complex spectra and add bounded multi-band circular drift.
   No source chunk is copied, reversed, alternated or time-stretched.
5. Reconstruct an exact-length circular buffer by wrapped overlap-add. Flatten applies a new
   circular macro envelope derived from the source's measured active range, not its original time
   order.
6. Source Match applies active-frame gated K-weighted loudness matching, broadband inter-channel
   correlation, Mid/Side width and left/right position matching at the requested depth. Silent
   one-shot tails do not become the target level.
7. Remove DC/non-finite samples, enforce a -1 dBTP circular true-peak ceiling, and reject results
   that fail closure, timbre, loudness, phase, position, stability or repeat-risk gates.

## Rotate & Repair algorithm

1. Treat the complete Source In/Out selection as the intended long loop.
2. Evaluate several overlap lengths for the bad original end-to-start seam using waveform, level,
   slope, spectrum, phase, transient and stereo evidence.
3. Scan internal cuts and prefer a stable, non-transient location for the new Loop Start.
4. Render `[Loop Start ... old end]`, repair the old end/start inside the result, then append
   `[old start ... Loop Start]`. The exported loop boundary is therefore an originally adjacent
   pair of source samples.
5. Remove DC/non-finite samples and enforce the same -1 dBTP circular true-peak ceiling.

## Parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `loopLength` | 0.25–16 s | 4 s | Secondary DAW-capture duration |
| `syncToHost` | off/on | on | Secondary capture alignment |
| `bars` | 1/2/4/8 | 1 | Secondary capture size |
| `crossfadeMs` | 1–250 ms | 25 ms | Rotate & Repair overlap limit |
| `mix` | 0–100% | 100% | Audition dry/generated mix only |
| `generationMode` | Rotate & Repair / Texture Loop | Rotate & Repair | Processing mode |
| `textureDuration` | 4–60 s | 24 s | Exact Texture Loop length |
| `variation` | 0–100% | 72% | Internal spectral movement depth |
| `flatten` | 0–100% | 72% | Removal of source macro dynamics/motion |
| `sourceMatch` | 0–100% | 85% | Loudness and spatial matching depth |

## Real-time and memory rules

- Decode, resampling, analysis, synthesis, state compression and WAV writing stay off the audio
  thread. The audio thread does not allocate, lock, perform file I/O or format logs.
- Two long texture candidates are retained using ownership swaps. At 60 seconds/48 kHz/stereo,
  source plus two float results are about 69 MB, excluding bounded workspaces and the capture
  buffer. Generation can temporarily use more while comparing a replacement candidate.
- Project state embeds the active output and source for portable recall; long sessions therefore
  increase DAW project size and save time.

## Unfinished release evidence

- This source revision has only static review. No compiler, unit test, VST3 validator, Windows
  REAPER, or Apple Silicon REAPER result exists yet.
- The K-weighted loudness and four-point circular true-peak implementations need numerical
  comparison with trusted meters before release.
- Spatial matching preserves robust band energy plus broadband position/correlation; it does not
  reproduce every frequency-dependent moving phase trajectory.
- A licensed multi-category corpus, blind comparisons with competent manual loops, long-duration
  listening and 44.1/48/96 kHz host coverage are still mandatory.
- Waveform zoom, typed/sample-level positions, snapping, nudge, undo/redo and WAV `cue`/`smpl`
  metadata remain unfinished.
- File decode/resampling, state compression and WAV export are synchronous UI/host-state jobs and
  can pause briefly on maximum-length files.
