# P2 Loop Surgeon

Current target: **0.8.0 pre-release**. Loop Surgeon is one VST3 effect/Standalone product with two
equally important modes. File import/drop is the primary workflow; DAW-input capture is secondary.

## Basic workflow

1. Drop or choose WAV, AIFF, FLAC, or OGG audio.
2. Drag blue **Source In/Out** to define the only material the engine may use.
3. Choose **Rotate & Repair** or **Texture Loop**.
4. Set the visible final length and mode controls, then press Generate.
5. Use **Source**, **Generated**, and **Preview/Stop** for A/B listening.
6. Drag the approved 24-bit WAV to the DAW or use **Save WAV...**.

The source, range, active result, parameters and R&R Loop Start are embedded in DAW project state.

## Rotate & Repair

Use R&R when the selected ambience/environment already contains the desired content, but its head
and tail do not join.

- **Final Length = Selection** preserves the complete blue range and performs the standard
  cut-rotate-overlap repair.
- A typed Final Length searches inside Source In/Out for the best contiguous span whose repaired
  output is sample-exactly that length.
- A length greater than Source In/Out is rejected. R&R never stretches, reverses, or secretly
  repeats material.
- The green **Loop Start** moves the completed loop start without creating a new discontinuity.

## Texture Loop

Use Texture when a source should become a clean, exact-length material layer without retaining its
ordered one-shot ADSR, pass-by, or directional macro trajectory.

- **Material = Auto** analyses signal structure and selects one of two source-domain engines.
- **Continuous** uses long forward exemplars, non-periodic scheduling, seam-aware transitions,
  envelope stabilisation, and only a quiet decorrelation bed.
- **Particles** detects source-local micro-events and reorganises them with bounded forward
  resampling, level, spacing, and stereo variation. It does not replace them with generated noise.
- **Output Length** is sample-exact.
- **Stability** controls removal of macro dynamics and trajectory while retaining local detail.
- **Transform** controls reorganisation/reconstruction depth. The stable parameter ID remains
  `sourceMatch` for project compatibility.
- **New Variation** changes the deterministic seed while retaining DAW recall.

The quality gate rejects non-finite samples, overload, weak closure, excessive repetition, stereo
damage, and outputs that match only a broad spectral colour while losing local material identity.

## Parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `generationMode` | R&R / Texture | R&R | Explicit product mode |
| `repairDuration` | Selection or 0.1–60 s | Selection | Exact R&R final length |
| `crossfadeMs` | 1–250 ms | 25 ms | Maximum R&R seam overlap |
| `textureStructure` | Auto / Continuous / Particles | Auto | Signal-structure path with manual override |
| `textureDuration` | 4–60 s | 24 s | Exact Texture final length |
| `flatten` | 0–100% | 72% | Texture Stability; ID retained for state compatibility |
| `sourceMatch` | 0–100% | 85% | Texture Transform; ID retained for state compatibility |
| `variation` | 0–100% | 72% | Deterministic material variation |
| `mix` | 0–100% | 100% | Audition mix only |

## Real-time and memory rules

- Decode, resampling, synthesis, state compression and WAV writing stay off the audio thread.
- The audio thread does not allocate, lock, perform file I/O, or format logs.
- Imported source and Texture output are limited to 60 seconds.
- Two texture candidates are retained. At 48 kHz stereo, a 60-second source plus two float results
  uses about 69 MB before temporary generation workspaces.

## Verified and unfinished evidence

- Windows Debug compilation and the deterministic engine suite pass locally for 0.8.0.
- Synthetic regression now covers sustained broadband, sparse resonant events, one-shot envelope
  removal, pass-by removal, deterministic recall, path override, closure, overload, repetition,
  local frame identity, and coloured-noise collapse.
- Two real SFX selections render through Auto and pass the automated 0.8 gates. These are engineering
  checks, not proof of subjective commercial quality.
- Fresh REAPER listening with the user's reported failing source, a licensed multi-category corpus,
  blind manual comparisons, VST3 Validator, trusted-meter comparison, 44.1/48/96 kHz hosts, and
  Apple Silicon REAPER remain required.
- Learned semantic separation, a dedicated harmonic-sustain third engine, waveform zoom,
  typed/sample-level range positions, snapping, nudge, undo/redo and WAV cue/smpl metadata remain
  unfinished.
