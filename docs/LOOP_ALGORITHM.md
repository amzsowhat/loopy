# Loop Surgeon algorithms

The user explicitly chooses between two equally important jobs. Source In/Out is a hard boundary.

## Rotate & Repair

With Final Length set to Selection, the complete source range is evaluated at several overlap
lengths. The engine chooses a safe internal rotation point, moves the original bad end/start join
inside the result, and crossfades it. The exported boundary is an originally adjacent source cut.

With an explicit Final Length, the engine scans contiguous windows inside Source In/Out. Each
candidate source span is `requested length + overlap`; render overlap therefore produces the exact
requested sample count. Impossible lengths are rejected. R&R never stretches, reverses or fills
missing duration by repetition.

## Texture Loop 0.7 hybrid reconstruction

1. Resample only Source In/Out and inspect up to 256 distributed active frames.
2. Reject silence and extreme hit-level outliers. Build robust Mid/Side stationary magnitude and
   broader material-colour models.
3. Detect locally prominent spectral peaks. Recreate these with phase-continuous trajectories so
   stable resonances survive without freezing one frame.
4. Recreate the remaining broadband residual with source-shaped stochastic phase. Slow movement
   interpolates between real analysed frame envelopes instead of replaying the original timeline.
5. Retain a forward-running exemplar path as the low-Rebuild detail layer. Rebuild crossfades from
   this source-local path toward the reconstructed resonant/stochastic model.
6. Apply smoothed spectral-colour correction, circular level stabilisation and bounded spatial
   matching. Generate 120 ms longer than requested, rotate at a stable internal cut, repair the
   construction boundary internally, and return the exact requested length.
7. Remove DC/non-finite samples, enforce a -1 dBTP ceiling, and score circular closure, material
   colour, loudness, phase, stereo position, stability and repeat risk.

## Current modelling boundary

This is a deterministic lightweight VST-compatible hybrid, not semantic source separation. It
cannot yet identify named layers such as ice crack, water, impact and reverb, nor can it preserve a
specific distribution of sparse micro-events independently of the bed. Automated QC rejects
structural failures but cannot certify subjective material quality; corpus listening remains a
release requirement.

