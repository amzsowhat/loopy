# Automatic loop algorithm

The primary path is file import, user Source In/Out, automatic search, optional manual Loop In/Out,
Original/Loop A/B, and WAV export. DAW capture is secondary.

1. Resample mono/stereo input to the host rate and bound analysis memory.
2. Build 100 Hz loudness, change-rate, and four-band spectral feature frames.
3. Normalize the feature streams and use multivariate autocorrelation to propose distinct periods.
4. Search start/end pairs across the user selection near each period.
5. Score direct sample jump, level, slope, short-window phase, spectrum, stereo correlation,
   transient continuity, duration, and period confidence.
6. Prune cheaply, evaluate detailed finalists, then refine both boundaries one sample at a time.
7. Return three candidates or an explicit low-confidence failure.
8. Include the repair overlap in the searched source span so the rendered loop keeps the intended
   period after crossfade.
9. Use linear overlap for highly phase-correlated seams and equal-power overlap otherwise. Preview
   and export share this rule.

This is an explainable deterministic heuristic, not a trained perceptual model and not a claim that
every source is loopable. The paid-beta blockers and honest limitations are tracked in
`plugins/loop-surgeon/docs/PRODUCT_REVIEW.md`.
