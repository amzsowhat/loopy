# loopy algorithms

## Rotate & Repair

Source In/Out is a hard boundary. With Final Length set to Selection, the engine selects an internal
adjacent cut, rotates the range so that cut becomes the exported boundary, then repairs the moved
original head/tail join. With an explicit Final Length it searches contiguous spans inside the
selection and returns the exact requested sample count.

Candidate comparison uses waveform, derivative, phase, spectrum and stereo differences only to
order possible joins. It is not exposed as a quality rating and never blocks export.

## Texture Loop: circular material-flow construction

1. Read only the selected Source In/Out waveform; mono or stereo channels remain phase-aligned.
2. Measure a slow RMS envelope and remove it by the Stability amount with bounded, smoothed gain.
3. Divide the conditioned waveform into overlapping long regions. Flow, Drift and Fracture change
   traversal scale, not material classification.
4. Choose the next region using boundary correlation, level/derivative continuity, recent-use
   avoidance and Transform-controlled departure from the original timeline.
5. Place every region into an exact-length circular buffer with a square-root Hann overlap. Regions
   crossing the end wrap through the beginning, so the loop boundary is part of the construction.
6. Apply bounded channel-energy balancing, DC/non-finite repair and a -1 dBTP ceiling.

The mode does not synthesize white noise, randomize FFT phase, add frequency-delay motion, quantize
to tempo, pitch-shift, stretch or reverse the source. It deliberately targets non-rhythmic SFX
material flow rather than electronic or EDM-style processing.
