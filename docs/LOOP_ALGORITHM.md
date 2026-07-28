# Loop Surgeon algorithms

## Stationary Texture

This is the default path for wind, rain, room tone, drones, and other noise-like material.

1. Resample mono/stereo input to the host rate and use only Source In/Out.
2. Sample at most 256 distributed 4096-point frames; discard silence and extreme level outliers.
3. Convert stereo to Mid/Side and calculate log-magnitude spectra.
4. Take the temporal median at every bin. Ordered rise/fall/pass-by trajectories are therefore not
   part of the model.
5. Lightly smooth across log frequency while retaining 75% of the measured bin detail.
6. Draw seeded Gaussian complex spectra around the Mid and Side models. Random phase makes
   neighbouring frames non-coherent and prevents the comb filtering created by overlapping copied
   grains.
7. Apply no more than subtle slow drift across ten broad frequency bands. Variation controls that
   drift, not source-event order.
8. Inverse-transform sine-windowed frames with 50% overlap directly into a circular output buffer.
   Wrapped frames straddle the end/start boundary and per-sample squared-window normalization keeps
   variance stable.
9. Reconstruct Left/Right, match active source RMS, cap peaks, and score the actual output.

## Direct Seam Loop

This is intentionally a conventional short-loop path for strongly periodic material.

1. Build 100 Hz loudness, change-rate, and four-band spectral feature frames.
2. Normalize the streams and use multivariate autocorrelation to propose periods.
3. Search strictly inside Source In/Out for start/end pairs near each period.
4. Score waveform jump, level, slope, phase, stereo, transient, repair, and period evidence.
5. Re-evaluate finalists using short/long windows and a 16-band spectral signature.
6. Test repair lengths up to the configured maximum against the rendered boundary.
7. Apply weak-link and activity penalties, refine at single-sample resolution, and expose the
   adopted bounds as green Loop In/Out markers.

Direct Seam Loop does not remove a pass-by, pitch sweep, or other event inside the chosen short
segment. Stationary Texture is the required mode when those event trajectories must disappear.

## Auto

Auto runs periodic analysis first. It keeps Direct Seam Loop only when the leading candidate has
strong periodicity, transient continuity, overall quality, and high confidence; otherwise it uses
Stationary Texture.

Both paths are deterministic explainable DSP, not trained perceptual models. Commercial claims
remain blocked on a larger licensed corpus and blind listening tests.
