# Loop Surgeon algorithms

The user explicitly chooses between two equally important jobs. Source In/Out is a hard boundary.

## Rotate & Repair

With Final Length set to Selection, the complete source range is evaluated at several overlap
lengths. The engine chooses a safe internal rotation point, moves the original bad end/start join
inside the result and crossfades it. The exported boundary is an originally adjacent source cut.

With an explicit Final Length, the engine scans contiguous windows inside Source In/Out. Each
candidate span includes the requested output plus repair overlap, so rendering returns the exact
sample count. Impossible lengths are rejected.

## Texture Loop 0.9 material resynthesis

1. Analyse overlapping Mid/Side STFT frames inside Source In/Out.
2. Remove macro level from the analysed states so the original ordered envelope does not become an
   output event train.
3. Learn normalized spectral states, persistent spectral shape, phase deviation, Mid/Side energy
   and channel balance. No named-material classifier is present.
4. Shuffle the state atlas once per deterministic seed. Smooth, random, circular splines traverse
   the atlas over the complete requested output length; they have no integer-rate sub-cycle.
5. Rebuild every frame with continuously accumulated phase. Style changes frequency-region
   coupling, trajectory density, resonance emphasis and phase memory. It never selects a source
   playback path.
6. Generate continuation audio past the requested boundary and fold it into the beginning with an
   equal-power circular overlap. Add only full-length closed macro movement.
7. Restore measured spatial energy, match bounded loudness, remove DC/non-finite values and enforce
   a -1 dBTP circular true-peak ceiling.
8. Score anomalous boundary discontinuity, source-window copying, lag-specific recurrence above the
   stationary baseline, material spectrum, local spectral identity, coloured-noise collapse,
   loudness and stereo position.

## Explicit non-goals

- reproducing a source event at irregular intervals;
- copying, reversing, stretching or mosaicing source windows;
- choosing an engine from a named material category;
- claiming subjective success from metrics alone.

The current engine is deterministic offline DSP. It is not a trained neural generator and does not
claim to reproduce proprietary Freakshow Industries or zynaptiq algorithms.
