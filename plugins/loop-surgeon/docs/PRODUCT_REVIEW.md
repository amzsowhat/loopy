# Loop Surgeon 0.7.0 pre-release review

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
- exact R&R Final Length search inside Source In/Out, with Selection as the default;
- Texture Loop hybrid phase-continuous resonance, stochastic residual and forward-detail paths;
- explicit Stability and Rebuild controls while retaining stable pre-release parameter IDs;
- material-colour correction plus bounded loudness, stereo correlation/width and position matching;
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

## Verification status

Windows x64 Release compilation and the deterministic engine suite pass locally. The supplied
underwater ice impact was also rendered through the offline Texture path. Its new source-frame
timbre score is materially higher than the removed random-phase renderer, while its 200 ms macro
level range remains controlled. This is automated evidence, not a substitute for listening.

VST3 Validator, Windows REAPER listening for 0.7.0, native Apple Silicon REAPER, trusted meter
comparison and a licensed multi-category listening corpus remain unverified.

## Remaining commercial blockers

1. Run VST3 Validator, Windows REAPER and native Apple Silicon REAPER, then fix host-specific
   failures.
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
