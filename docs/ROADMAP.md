# Roadmap

## P2 Loop Surgeon

### Demo 0.1 — implemented

- VST3 and Standalone targets
- mono/stereo host layouts
- input capture with configurable duration
- seam crossfade and wet/dry mix
- basic seam-quality indicator
- host parameter-state round trip
- deterministic engine tests and Windows CI

### Prototype 0.2

- waveform overview with draggable loop markers
- automatic end-point search around the captured duration
- level, slope, spectral, phase, and stereo seam sub-scores
- host-loop capture mode using sample-position context
- non-destructive capture asset persistence

### Product 0.3

- local gain and spectral seam repair
- salient-event lane and protected regions
- long-duration repeat-risk analysis
- cue/smpl marker and sidecar export on a background worker
- compatibility matrix across supported DAWs

