# Loop Surgeon 0.6.0 pre-release review

## Product correction

The old short-period Direct Seam path did not represent the sound-design workflow. It could return
a tiny repeated fragment even when the user supplied a complete ten-second ambience. The product
now has two explicit, equally weighted modes:

- **Rotate & Repair** automates the established long-loop edit: keep the complete selected ambience,
  rotate at a naturally continuous internal point, and repair the moved old head/tail seam.
- **Texture Loop** creates an exact-length stationary or gently moving layer from source material
  while discarding the source's ordered ADSR/pass-by timeline.

There is no Auto mode. The user selects the treatment appropriate to the source.

## Implemented in source

- fixed blue Source In/Out and one draggable green Loop Start;
- load-only file import followed by an explicit Generate action, so mode and range are chosen before
  expensive processing starts;
- full-selection forward-only Rotate & Repair with adaptive internal seam overlap;
- Texture Loop temporal-median spectral modelling, isolated-peak smoothing, random complex spectra,
  circular multiscale drift and circular overlap-add;
- explicit Flatten and Source Match controls;
- active-frame gated K-weighted loudness matching, stereo correlation/width and left/right position
  matching with one user Source Match depth;
- DC/non-finite repair and a -1 dBTP circular oversampled peak ceiling;
- repeat-risk, closure, timbre, loudness, phase, position and stability gates that block export;
- real source/output spectrum overlay, phase scope, correlation and position display;
- two retained deterministic alternatives instead of three, reducing persistent maximum texture
  memory by about one third;
- Preview/Stop, Source/Generated A/B, immediate Clear Result, 24-bit Save WAV and external Drag Loop
  to DAW;
- project state that embeds the active output, source, range and Loop Start for portable recall;
- automatic GitHub Actions triggers removed from all three workflows so source synchronization does
  not consume the exhausted account quota.

## What has not been verified

This revision was intentionally not built. No compiler result, deterministic-test result, plug-in
validator result, packaged VST3 or REAPER session exists for 0.6.0 yet. All items above mean
implemented in source and statically inspected, not proven working.

The earlier 0.5.2 wind experiment showed that spectral reconstruction could reduce copied pass-by
correlation substantially, but those figures cannot validate the new code. The new loudness,
spatial, state and Rotate & Repair paths require fresh measurements.

## Remaining commercial blockers

1. Compile and run deterministic tests on Windows and Apple Silicon after build capacity is
   available, then fix all compiler/test failures.
2. Validate the K-weighting and true-peak results against trusted reference meters.
3. Test the supplied whoosh plus a licensed multi-category corpus; include blind manual-loop A/B,
   repeated-boundary listening and at least 44.1/48/96 kHz.
4. Measure frequency-dependent stereo/phase preservation. The current model combines band-specific
   Mid/Side energy with broadband position/correlation correction; moving spatial trajectories are
   intentionally removed and detailed cross-spectral covariance is not yet reconstructed.
5. Measure peak persistent and temporary memory, DAW project size, save latency and restore latency
   at the 60-second ceiling.
6. Add cancellation inside a single synthesis render and move import/export/state serialization to
   cancellable background jobs. Current cancellation occurs only between candidate attempts.
7. Add waveform zoom, typed/sample positions, snapping, keyboard nudge, undo/redo and WAV
   `cue`/`smpl` metadata.
8. Complete Windows REAPER and native Apple Silicon REAPER host matrices before signing or selling.

## Current quality verdict

The architecture now matches the two stated sound-design jobs and removes the old conceptual error.
The code is still a pre-release candidate. Calling it commercially finished before compilation,
meter comparison and real listening would be unsupported.
