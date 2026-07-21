# Roadmap

## P2 Loop Surgeon

### Prototype 0.2 鈥?implemented

- VST3 and Standalone targets
- mono/stereo host layouts
- one-click capture with automatic start/end search around the requested duration
- level, slope, spectral, phase, and stereo seam sub-scores
- background analysis with no large clear or analysis pass on the audio thread
- seam crossfade, smoothed wet/dry mix, and automatic playback
- host parameter and gzip-compressed captured-audio state round trip
- deterministic engine tests and Windows CI
- universal macOS CMake presets for Reaper testing

### Prototype 0.3

- waveform overview with draggable loop markers
- host-loop capture mode using sample-position context
- bar/beat quantization and transport-aligned capture
- multi-resolution FFT and perceptual weighting for difficult sources
- higher-quality state restore resampling

### Product 0.4

- local gain and spectral seam repair
- salient-event lane and protected regions
- long-duration repeat-risk analysis
- cue/smpl marker and sidecar export on a background worker
- compatibility matrix across supported DAWs
