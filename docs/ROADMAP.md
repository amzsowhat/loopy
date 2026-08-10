# Roadmap

## P2 Loop Surgeon

### 0.6.0 pre-release source

- one VST3/Standalone product with two equal modes: Rotate & Repair and Texture Loop;
- fixed Source In/Out selection in both modes;
- full-selection circular rotation plus internal head/tail repair for designed ambience loops;
- exact-length stationary texture synthesis for removing one-shot ADSR/pass-by motion;
- user Flatten and combined Source Match depth for loudness, phase/correlation, stereo position and
  channel balance;
- source/generated audition with explicit Preview/Stop;
- circular true-peak ceiling, closure, spectrum, loudness, phase, position, stability and repeat-risk
  quality gates;
- real spectrum, phase, correlation and position views;
- 24-bit WAV export, DAW file drag and embedded project recall;
- automatic GitHub Actions triggers disabled while the account quota is exhausted.

This revision is source-only. It has not been compiled, packaged, listened to, meter-validated or
host-validated.

### Required before a sale candidate

- compile and run deterministic tests on Windows x64 and Apple Silicon;
- fix all compiler, test, validator and REAPER failures;
- compare K-weighted loudness and circular true-peak estimates with trusted meters;
- test a licensed multi-category source corpus at 44.1, 48 and 96 kHz;
- run blind A/B against competent manual loops and long-duration repeat listening;
- measure memory, project size, save/restore latency and offline generation latency;
- validate DAW recall, automation, export/reimport and drag delivery;
- complete signing, notarisation, installer, licensing and support documentation.

### Later product work

- frequency-dependent cross-channel coherence/covariance matching;
- cancellable long synthesis and background import/export/state serialization;
- waveform zoom, typed sample/time positions, snapping, keyboard nudge and undo/redo;
- WAV `cue`/`smpl` metadata and batch/library workflows.
