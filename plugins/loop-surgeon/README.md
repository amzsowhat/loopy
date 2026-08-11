# P2 Loop Surgeon

Current active plug-in path: **Rotate & Repair only**. The previous Texture 0.9 engine has been
withdrawn after failed listening tests.

## R&R workflow

1. Drop or choose WAV, AIFF, FLAC, or OGG audio.
2. Drag Source In/Out to define the material range.
3. Set Final Length to Selection or type an exact shorter duration.
4. Generate, audition Source and Generated, then adjust Loop Start if needed.
5. Save a 24-bit WAV or drag the render into the DAW.

R&R keeps the selected audio in forward order. It rotates an originally adjacent internal cut to
the exported boundary, moves the bad original end/start join inside the result and repairs that join
with a short overlap. Candidate ranking is internal seam-search math; it is never presented as an
audio-quality grade.

## Texture withdrawal

- The Texture option is disabled in the UI and ignored by the processor.
- `Loop-Surgeon-0.9.0-pre-Windows-x64-VST3.zip` is quarantined under
  `build/obsolete/failed-texture-0.9/` locally.
- No quality total, PASS badge, subjective sub-score or export gate remains in the plug-in.
- Spectrum, phase plot, stereo correlation, position, finite-number repair, DC removal and true-peak
  ceiling remain signal diagnostics or safety processing. They do not judge usefulness or taste.

## Replacement research

`Research/SpectralOrbitPrototype.*` is an offline, input-driven spectral-time experiment inspired by
the product behavior demonstrated in Ambisonar Prism: an explicit frequency-to-delay curve moves
complex source spectra around a closed time ring with feedback. It does not randomize FFT-bin phase,
tile source clips or classify source material.

The lab exposes three curve geometries (`sweep`, `fold`, `barberpole`) and writes WAV files. These are
listening candidates only. They are not wired into the VST3 and carry no commercial or audio-quality
claim.

## Verified locally

- Windows Debug VST3 and Standalone compile.
- R&R deterministic/state tests pass.
- The offline Spectral Orbit renderer compiles and writes finite 24-bit WAV files.
- No Windows package is currently designated as a sale or Texture test build.

