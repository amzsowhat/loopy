# Automatic loop algorithm

## Product intent

The primary path is one-click loop creation. The user chooses a musical size (host-synced bars by
default) or an approximate duration, presses capture, and receives an automatically selected loop.
Manual start/end editing is optional future correction, not the core workflow.

## Capture and alignment

When host sync is enabled, the processor reads BPM, PPQ position, and time signature. Capture is
armed for the next bar boundary and the requested duration is calculated from 1, 2, 4, or 8 bars.
If the host does not expose valid timing, the plug-in falls back to the duration parameter.

The engine records the requested duration plus a 150 ms search margin on both sides. This gives the
analyzer room to move both the start and end instead of forcing a seam at the button click.

## Candidate search

The worker evaluates candidate start positions and lengths on a 5 ms grid. A two-stage search keeps
CPU bounded:

1. All candidates receive inexpensive level, slope, phase-correlation, stereo-continuity, and
   duration scores.
2. The best 12 candidates receive an additional eight-band spectral comparison.
3. Weighted component scores select the final start and end.

Weights currently favor phase and spectrum, followed by level, slope, stereo continuity, and a
small penalty for moving too far from the requested musical duration.

## Component scores

- **Level:** compares RMS energy at the head and tail.
- **Slope:** compares the first derivative at the exact boundary to detect click-producing jumps.
- **Phase:** normalized cross-correlation of short head and tail windows.
- **Spectrum:** compares Hann-windowed deterministic frequency-band magnitudes.
- **Stereo:** compares left/right correlation at the head and tail so image width does not jump.
- **Crossfade:** blends the selected tail into the selected head during playback.

## Novelty and expected quality

This is an explainable hybrid heuristic, not a new academic algorithm. Its value is the product
workflow: host-aligned capture, automatic two-dimensional boundary search, multi-domain scoring,
background processing, immediate audition, and project persistence in one plug-in.

It should work well for sustained textures, ambience, pads, drones, steady percussion, and material
with repeated structure. It cannot guarantee a convincing result for speech, one-shot transients,
strongly evolving melodies, or audio with no repeated region. A later version should add
multi-resolution FFT features, transient protection, tempo-aware similarity, and perceptual ranking
across several returned candidates.
