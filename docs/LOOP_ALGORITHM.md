# Loop Surgeon algorithms

## Evolving Texture

This is the default imported-source path for wind, rain, room tone, drones, and other reasonably
stationary material.

1. Resample imported mono/stereo audio to the host rate and limit it to 60 seconds.
2. Use only the user-selected Source In/Out range.
3. Estimate a 160 ms slow RMS envelope. Reject near-silent, attack, decay-tail, and strongly
   changing candidate regions so a one-shot's macro envelope is not copied repeatedly.
4. Smoothly invert that envelope with bounded gain, shared by all channels. This creates a
   stationary timbral carrier while preserving short-term noise detail and stereo relationships.
5. Adapt a long grain size from about 0.42 to 1.35 seconds. Long grains preserve environmental
   timbre better than a dense cloud of 10–100 ms grains.
6. Measure each candidate head/tail using level, derivative energy, stereo correlation, and an
   eight-band spectral signature.
7. Build a seeded path. Acoustic transition distance ranks candidates; Variation widens the
   top-ranked choice set. Penalties discourage immediate reuse, recent reuse, near-identical source
   positions, and simply continuing through the source in order.
8. Include transition-back-to-start cost near the end of the path.
9. Render every grain into a circular output buffer with overlapping windows and per-sample
   normalization. The full 4–60 second result is therefore the loop, not a short segment repeated
   until the requested duration.
10. Measure the circular output with a 360 ms envelope and apply a bounded, circularly smoothed
    correction. This suppresses repeated macro swells without flattening microtexture.
11. Generate three deterministic seeds. Candidate switching swaps buffer ownership instead of
   duplicating the active long buffer.

## Seam Loop

This path remains for strongly periodic material.

1. Build 100 Hz loudness, change-rate, and four-band spectral feature frames.
2. Normalize the streams and use multivariate autocorrelation to propose periods.
3. Search Source In/Out for start/end pairs near each period.
4. Score waveform jump, level, slope, phase, stereo, transient, repair, and period evidence.
5. Re-evaluate finalists using short/long windows and a 16-band spectral signature.
6. Test several repair lengths up to the configured maximum against the actual rendered boundary.
7. Apply weak-link and activity penalties, then refine boundaries at single-sample resolution.

## Auto

Auto runs the periodic analysis first. It keeps Seam Loop only when the leading candidate has strong
periodicity, transient continuity, overall quality, and high confidence; otherwise it selects
Evolving Texture. The chosen mode is reported by the engine and can be overridden by the user.

Both paths are deterministic explainable DSP, not trained perceptual models. Commercial claims
remain blocked on a licensed corpus and blind listening tests.
