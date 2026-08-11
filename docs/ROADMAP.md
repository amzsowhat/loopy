# Roadmap

## Current baseline

- Keep Rotate & Repair usable and sample-exact.
- Keep the rejected Texture engine disabled.
- Do not publish or package a Texture build from the 0.9 code path.
- Do not use self-authored audio-quality totals, PASS badges or subjective thresholds.

## Creative-loop replacement gate

1. Render offline WAV candidates from one fixed, source-agnostic algorithm.
2. Confirm by listening that the result has an authored spectral-time character, does not become
   industrial noise and does not expose regular source replay.
3. Reject the hypothesis if the listening result fails; do not compensate with more UI or metrics.
4. After the sound is accepted, expose output length, editable delay curve, feedback, diffusion,
   Barberpole/wrap, stereo link and wet/dry in the plug-in.
5. Then implement DAW recall, automation, cancellation, memory bounds and local host validation.

## Before sale

- Windows and Apple Silicon release builds; REAPER validation at 44.1/48/96 kHz.
- VST3 validator, crash recovery, save/restore, export/reimport and drag tests.
- Long listening sessions and blind comparisons against competent manual work and established tools.
- Signing, notarisation, installers, licensing, support and licensed test material.

