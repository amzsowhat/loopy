# Automatic loop algorithm

The primary path is file import, user Search In/Out, automatic search, optional manual Loop In/Out,
audition, and WAV export. DAW capture is secondary.

## 0.4 analysis path

1. Decode mono/stereo input, resample it to the host rate with a high-order windowed-sinc
   interpolator, and cap imported analysis memory at 60 seconds.
2. Build 100 Hz loudness, change-rate, and four-band spectral feature frames.
3. Normalize every feature stream and use multivariate autocorrelation to propose distinct periods.
4. Search start/end pairs across the blue user range near each proposed period.
5. Prune broad candidates with cheap waveform, level, slope, phase, stereo, transient, repair, and
   period evidence.
6. Re-evaluate finalists with short and long windows plus a 16-band spectral signature.
7. Test multiple repair lengths up to the user's **Max Repair Window**. The repair score simulates
   the actual linear/equal-power overlap and measures the final rendered boundary, not just the raw
   source endpoints.
8. Apply a weak-link penalty across waveform, phase, transient, and repair evidence so one severe
   defect cannot be hidden by several unrelated high scores. A source-relative activity penalty
   prevents silent regions from receiving a misleading perfect score.
9. Refine both boundaries at single-sample resolution and return up to three diverse candidates.
10. Keep the repair overlap inside the searched source span so the rendered loop retains the
    detected period after overlap.
11. Use linear overlap for highly phase-correlated seams and equal-power overlap otherwise.
    Real-time preview begins on the same repaired sample as the exported WAV.

Manual green Loop In/Out edits are evaluated without returning to an automatic default. The chosen
repair length remains frozen for the current candidate; changing **Max Repair Window** affects the
next Find Best Loop or Use Manual Loop operation.

This remains an explainable deterministic heuristic, not a trained perceptual model and not a claim
that every source is loopable. A paid release still requires a licensed real-world corpus and blind
listening validation; those blockers are tracked in
`plugins/loop-surgeon/docs/PRODUCT_REVIEW.md`.

