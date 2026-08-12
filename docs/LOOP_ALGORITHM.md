# Loop Surgeon algorithms

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
4. Choose the next region by comparing the complete overlap that will actually be heard, then refine
   its source position at sample scale. The score also includes level/derivative continuity,
   recent-use avoidance, final-to-first closure and Transform-controlled timeline departure.
5. Place longer regions around an exact-length circle with short complementary crossfades and a
   single-material plateau between joins. This replaces the former long multi-region overlap that
   could create comb filtering, beating and a synthetic/electronic colour.
6. Optionally apply Crush: two circular, stereo-linked RMS-envelope correction scales reduce smaller
   ADSR pulses. At 0% this stage is an exact bypass.
7. Optionally run Extra after Natural: Patina adds nonlinear memory colour, Bloom adds circular
   source-derived diffusion, and Fray exposes short material detail. Off and 0% are exact bypasses.
8. Apply bounded channel-energy balancing, DC/non-finite repair and a -1 dBTP ceiling.

The mode does not synthesize white noise, randomize FFT phase, add frequency-delay motion, quantize
to tempo, pitch-shift, stretch or reverse the source. It deliberately targets non-rhythmic SFX
material flow rather than electronic or EDM-style processing.
