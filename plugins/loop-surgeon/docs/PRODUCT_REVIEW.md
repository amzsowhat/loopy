# Loop Surgeon 0.5.2 product review

## Product boundary

Loop Surgeon has two deliberately different outputs:

- **Stationary Texture** turns the timbre of a selected one-shot or short recording into a longer,
  circular sound bed. It is intended for wind, noise, rooms, drones, machinery, water, and other
  sustained sound-design material. The source's attack, decay, pass-by, rise, and hit trajectory
  should not be copied into the result.
- **Direct Seam Loop** is a conventional short-loop tool for already-periodic or steady material.
  It searches only inside the blue Source In/Out range, exposes the chosen green Loop In/Out range,
  and repairs that boundary. It does not invent ongoing variation.

The product is not a keyboard sampler, a neural generator, or a promise that every tonal, speech,
or transient source can become a natural ambience.

## Implemented in 0.5.2

- draggable Source In/Out selection used as a hard analysis boundary;
- visible Loop In/Out markers for Direct Seam Loop;
- Auto, Stationary Texture, and Direct Seam Loop stored under the existing stable parameter ID;
- mode and generation controls synchronised when Generate is clicked, including while DAW playback
  is stopped;
- distributed source-frame analysis with silence and extreme-hit rejection;
- Mid/Side 4096-point FFT model built from temporal median log magnitudes;
- mostly unsmoothed source spectral detail plus light log-frequency regularisation;
- seeded non-coherent Gaussian spectra and random phase, avoiding correlated grain overlap;
- subtle slow multiband drift controlled by Variation;
- circular overlap-add synthesis with per-sample window-power normalisation;
- three deterministic variations, project recall, generated/source audition, Stop, and 24-bit WAV
  export;
- mode-specific quality labels instead of presenting seam metrics as texture quality;
- deterministic regression tests for recall, seed difference, circular closure, pass-by reduction,
  hidden-cycle rejection, source-range bounds, candidate switching, playback, and state.

## Evidence from the supplied wind whoosh

The old long-grain render repeated spectral groups and retained the source pass-by trajectory. Its
copied-window correlations were about 0.37 for spectral centroid and 0.47 for RMS, and correlated
overlaps created comb-like tonal prominence.

Using the first 5.85 seconds of the supplied source, the 0.5.2 C++ spectral render reduced those
same repeat correlations to about 0.07 and 0.07. Its circular boundary jump was below the median
ordinary adjacent-sample difference in both channels. This is strong evidence that the particular
repeated pass-by pattern and old boundary fault were removed; it is not yet a broad commercial
quality claim.

## Paid-beta blockers

1. A licensed evaluation corpus covering wind, rain, surf, crowds, rooms, engines, machines,
   drones, tonal beds, stereo ambience, impulsive contamination, speech, and very short sources.
2. Blind comparisons against hand edits and established texture/granular tools, scored for source
   identity, electronic tonality, repetition, pumping, image motion, and boundary audibility.
3. Frequency-dependent stereo-coherence modelling. The current Mid/Side statistical model can
   preserve broad width but not every band-specific spatial relationship.
4. Content classification and clearer rejection guidance for melody, speech, hard transients, and
   sources with insufficient stationary material.
5. A better Direct Seam Loop audition mode that solos several boundary crossings and makes its
   intentionally short, periodic nature obvious before export.
6. Cancellation and progress reporting for analysis, decoding, resampling, and export.
7. Waveform zoom/pan, typed positions, optional transient or zero-crossing snapping, keyboard
   nudge, and undo/redo.
8. Full spectrum, phase, and correlation inspection plus long-duration audition.
9. Export `cue`/`smpl` metadata and a portable source/recipe option.
10. Verified Windows/Reaper and Apple Silicon/Reaper compatibility, project recall, and sample-rate
    transitions across a wider real-host matrix.

## Honest quality assessment

0.5.2 removes the core architectural mistake in 0.5.0/0.5.1: Stationary Texture no longer assembles
copied long grains. It models the selected source's spectral distribution and synthesises a new
circular signal, which is the correct direction for making a source-coloured sustained bed without
replaying its time trajectory.

Direct Seam Loop remains intentionally conventional. On non-periodic wind it can still sound like
a short repeated fragment; Stationary Texture is the intended mode for that use case. A generated
WAV also repeats when its complete duration ends. Commercial readiness still depends on corpus
testing and blind listening, particularly for stereo behaviour and tonal or structured sources.
