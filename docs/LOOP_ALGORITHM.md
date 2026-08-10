# Loop Surgeon algorithms

Loop Surgeon exposes two equal, user-selected modes. Auto routing was removed because the two jobs
have different creative intent that the source waveform alone cannot determine reliably.

## Rotate & Repair

Use this when Source In/Out already contains the intended long ambience or environment edit.

1. Evaluate the original end/start mismatch with short and longer waveform, level, derivative,
   spectrum, transient, phase and stereo windows.
2. Test several overlap lengths up to Seam Repair and keep the least destructive internal repair.
3. Scan internal source cuts for stable level/spectrum/stereo context and low transient energy.
4. Rotate the complete forward-running range at the chosen cut.
5. Crossfade the old end into the old start inside the result. The final loop boundary now joins
   the two source samples that were adjacent at the internal cut.
6. Remove non-finite samples/DC and enforce a circular -1 dBTP peak ceiling.

This automates the standard manual cut-rotate-overlap workflow and does not extract a short period.
The overlap shortens the result slightly; the UI reports actual output length.

## Texture Loop

Use this when the goal is a sustained layer made from a source's material without its original
ADSR, pass-by or directional timeline.

1. Use only Source In/Out and sample up to 256 distributed active 4096-point frames.
2. Reject silence and extreme hit outliers. Take per-bin temporal medians of Mid/Side log spectra.
3. Adaptively smooth isolated narrow peaks while retaining broad spectral colour.
4. Draw deterministic non-coherent Gaussian complex spectra around those models. No source segment
   is copied, reversed or time-stretched.
5. Apply multi-rate integer-cycle spectral drift, then circular sine-window overlap-add with
   per-sample energy normalization.
6. Measure the source's active macro-level range. Flatten controls a newly generated circular
   movement envelope; the ordered source envelope is never reused.
7. Source Match controls active-frame gated K-weighted loudness correction, Mid/Side width,
   broadband channel correlation and left/right energy position. The ordered one-shot envelope and
   silent tail are excluded from the loudness target.
8. Remove non-finite samples/DC, enforce a circular -1 dBTP ceiling and score closure, timbre,
   loudness, phase, position, stability and repeat risk.

The exported WAV eventually repeats at its full requested length. The target is absence of an
obvious shorter envelope or spectral cycle inside that buffer.

## Known modelling boundary

Texture Loop preserves band-specific Mid/Side energy and broadband position/correlation. It does
not yet reconstruct a full frequency-dependent complex stereo covariance matrix. The source code
also still needs fresh compiler, meter, corpus and host validation before commercial release.
