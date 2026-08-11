# P2 Loop Surgeon

Current target: **0.9.0 pre-release**. Loop Surgeon is a VST3 effect and Standalone renderer with
two equal modes. File import/drop is the primary input path.

## Workflow

1. Drop or choose WAV, AIFF, FLAC, or OGG audio.
2. Drag **Source In/Out** to define the analysed range.
3. Choose **Rotate & Repair** or **Texture Loop**.
4. Set final length and the visible mode controls, then press Generate.
5. Compare Source and Generated, then drag or save the approved 24-bit WAV.

Source, range, active result, parameters and R&R Loop Start are embedded in DAW project state.

## Rotate & Repair

R&R automates the established long-loop edit. It keeps the selected content in forward order,
rotates an originally adjacent internal cut to the output boundary and repairs the moved original
head/tail seam. Final Length may preserve the selection or request an exact shorter result.

## Texture Loop

Texture treats the selected source as analysis material for a new generative sound. Source samples
are never copied, reversed, tiled, stretched or scheduled onto the output timeline.

- **Organism** balances shared spectral movement and independently evolving frequency regions.
- **Spectral Drift** uses fewer, slower coupled regions for a smoother sustained body.
- **Fracture** uses a denser independently moving spectral field for a more transformed result.
- **Output Length** is the exact loop duration.
- **Stability** controls closed macro movement.
- **Transform** controls resonance emphasis, spectral mutation and source phase-memory depth.
- **New Variation** changes the deterministic model trajectory while preserving DAW recall.

All styles use one source-agnostic analysis pipeline. There are no material names, classifiers or
per-source thresholds. The engine analyses normalized spectral states, phase evolution, persistent
spectral shape and Mid/Side energy. A deterministic closed spline drives phase-continuous offline
resynthesis across the complete requested duration. The only exact trajectory period is the final
loop length.

## Parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `generationMode` | R&R / Texture | R&R | Product mode |
| `repairDuration` | Selection or 0.1–60 s | Selection | Exact R&R length |
| `crossfadeMs` | 1–250 ms | 25 ms | Maximum R&R seam overlap |
| `textureStructure` | Organism / Spectral Drift / Fracture | Organism | Resynthesis style; ID retained for state compatibility |
| `textureDuration` | 4–60 s | 24 s | Exact Texture length |
| `flatten` | 0–100% | 72% | Stability; ID retained for state compatibility |
| `sourceMatch` | 0–100% | 85% | Transform; ID retained for state compatibility |
| `variation` | 0–100% | 72% | Trajectory variation |
| `mix` | 0–100% | 100% | Audition mix |

## Verified locally

- Windows Debug VST3, Standalone, offline renderer and deterministic tests build successfully.
- Tests cover exact duration, deterministic recall, distinct style output, source-window copy guard,
  source-duration recurrence, closed boundary, peak, finite samples, stereo/position preservation,
  noise-collapse risk and R&R state recall.
- A 48 kHz source-agnostic signal probe renders all three styles. Automated measurements pass for
  Organism after the recurrence and stereo corrections; this is engineering evidence, not a
  listening verdict.

## Still required before sale

- Hands-on REAPER listening over a licensed, balanced corpus and blind comparison against manual or
  established commercial workflows.
- Windows VST3 Validator, 44.1/48/96 kHz host coverage and trusted-meter comparison.
- Native Apple Silicon build and REAPER validation.
- Peak temporary-memory and DAW-state-size measurement at the 60-second ceiling.
- Subjective approval of every style. A passing metric cannot establish a successful sound.
