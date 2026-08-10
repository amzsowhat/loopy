­r‡^Ñf¥–Ø¦{~,yÊ'vÃ®¶›­# P2 Loop Surgeon

Current target: **0.7.0 pre-release**. Loop Surgeon is one VST3 effect/Standalone product with two
equally important modes. File import/drop is the primary workflow; DAW-input capture is secondary.

## Basic workflow

1. Drop or choose WAV, AIFF, FLAC, or OGG audio.
2. Drag blue **Source In/Out** to define the only material the engine may use.
3. Choose **Rotate & Repair** or **Texture Loop**.
4. Set the visible final length and mode controls, then press the single Generate button.
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

Use Texture when a one-shot or moving source should become a clean sustained material layer.

- **Output Length** is sample-exact.
- **Stability** controls removal of the source macro envelope/pass-by movement.
- **Rebuild** moves from forward-running source exemplars toward a reconstructed material model.
  At 100%, the engine prioritises a clean, defined texture over preservation of the source event
  sequence or waveform.
- **New Variation** changes the deterministic seed while retaining project recall.

The 0.7 engine contains three internal paths:

1. A phase-continuous resonant layer preserves stable narrow-band material cues without freezing a
   single sample frame.
2. A stochastic residual layer carries broadband air/water/noise texture using the measured source
   spectral envelope instead of white/pink-noise substitution.
3. A low-level forward exemplar layer restores source-local detail at lower Rebuild values.

The layers are colour-matched, circularly level-stabilised, spatially constrained, generated longer
than requested, internally seam-repaired, and then reduced to the exact requested duration.

## Parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `generationMode` | R&R / Texture | R&R | Explicit creative intent |
| `repairDuration` | Selection or 0.1â€“60 s | Selection | Exact R&R final length |
| `crossfadeMs` | 1â€“250 ms | 25 ms | Maximum R&R seam overlap |
| `textureDuration` | 4â€“60 s | 24 s | Exact Texture final length |
| `flatten` | 0â€“100% | 72% | Texture Stability; ID retained for state compatibility |
| `sourceMatch` | 0â€“100% | 85% | Texture Rebuild; ID retained for state compatibility |
| `variation` | 0â€“100% | 72% | Host-automatable material movement/variation |
| `mix` | 0â€“100% | 100% | Audition mix only |

## Real-time and memory rules

- Decode, resampling, synthesis, state compression and WAV writing stay off the audio thread.
- The audio thread does not allocate, lock, perform file I/O, or format logs.
- Imported source and Texture output are limited to 60 seconds.
- Two texture candidates are retained. At 48 kHz stereo, a 60-second source plus two float results
  uses about 69 MB before temporary generation workspaces.

## Verified and unfinished evidence

- Windows x64 Release VST3 and deterministic engine tests pass locally for 0.7.0.
- The supplied underwater ice impact renders through the offline 0.7 path and passes automated
  closure, true-peak, spectrum-colour, stereo, stability and repeat-risk gates.
- Automated gates do not prove subjective commercial quality. Fresh REAPER listening on the ice
  source, a licensed multi-category corpus, blind comparisons, VST3 Validator, trusted-meter
  comparison, 44.1/48/96 kHz host coverage and Apple Silicon REAPER remain required.
- The present Texture engine is a first hybrid reconstruction implementation. It does not yet
  perform learned semantic material separation, modal tracking, sparse micro-event detection or a
  neural offline enhancement pass.
- Waveform zoom, typed/sample-level range positions, snapping, nudge, undo/redo and WAV cue/smpl
  metadata remain unfinished.

