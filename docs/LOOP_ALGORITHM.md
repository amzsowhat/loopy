# Loop Surgeon algorithms

## Rotate & Repair — active

Source In/Out is a hard boundary. With Final Length set to Selection, the complete range is examined
at several short overlap lengths. The engine selects an internal adjacent cut, rotates the range so
that cut becomes the exported boundary, then repairs the moved original head/tail join.

With an explicit Final Length, the engine scans contiguous spans inside Source In/Out. Each span
contains the requested output plus repair overlap, so the rendered sample count is exact. The search
uses waveform, slope, phase, spectral and stereo differences as an internal objective function. That
number only orders candidates; it is not shown to the user and is not an audio-quality verdict.

## Texture 0.9 — rejected

The rejected engine shuffled normalized STFT states, moved frequency regions on independent closed
trajectories and reconstructed bins with synthetic phase evolution. It suppressed direct source
repetition but destroyed too much cross-frequency phase and temporal microstructure. Listening
results were industrial/noise-like and materially unrecognisable. It is withdrawn rather than tuned.

## Spectral Orbit lab — listening hypothesis

1. Analyse the input into overlapping complex STFT frames without randomizing bin phase.
2. Map frequency to delay using one explicit user-shapeable curve.
3. Move each complex coefficient around a closed output-time ring and apply the phase rotation of
   the actual delay.
4. Recirculate the transported spectrum for a bounded number of feedback laps; the ring wraps by
   construction and creates sweep, fold or Barberpole trajectories.
5. Reconstruct with circular overlap-add, remove DC/non-finite samples, match bounded RMS and enforce
   a -1 dBTP ceiling.

The current lab is not yet accepted. Human listening decides whether the mechanism is useful and has
a coherent character. Automated checks are limited to file validity and numeric safety.

