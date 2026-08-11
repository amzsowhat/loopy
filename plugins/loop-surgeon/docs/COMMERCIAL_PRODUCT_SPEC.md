# Loop Surgeon commercial product contract

This document is the release boundary. Both modes are first-class parts of one plug-in. A version
is not called sale-ready until every mandatory item has objective checks and hands-on DAW listening
evidence.

## Product form and delivery

Loop Surgeon is a VST3 audio effect and loop renderer, not a MIDI sampler. In REAPER it can be Track
FX or Take FX, but a standard VST3 cannot directly inspect or replace an entire DAW item. Audio
therefore enters through file import/drop or input capture. **Drag Loop to DAW** is the primary DAW
delivery path; **Save WAV...** serves libraries and Standalone use.

## Equal processing modes

### Rotate & Repair

For an already-designed long ambience/environment selection. The complete Source In/Out selection
must remain in forward order. The algorithm moves the bad original end/start join inside the result,
repairs that join, and uses an originally adjacent internal cut as the new editable Loop Start. It
must not collapse the selection into a short repeated fragment.

### Texture Loop

For converting a source event into a sustained layer of the same material. It must remove ordered
ADSR, pass-by and directional trajectories without reversing or simply time-stretching the source.
Output length is exact. Stability controls macro movement removal; Transform moves from source-local
exemplars toward deeper source-domain reorganisation. Auto analyses signal structure and chooses
Continuous or Particles; the user can override that choice. Neither path may substitute generic
generated noise for source-local material identity.

## Primary flow

1. Choose either mode and load a source. File import only loads; it does not start processing.
2. Set blue Source In/Out.
3. Set mode-specific controls and explicitly generate. DAW input capture generates after recording
   completes because capture itself is the requested action.
4. Compare Source and Generated with an explicit Preview/Stop transport.
5. Inspect real source/output spectrum, phase scope, stereo correlation and position evidence.
6. In Rotate & Repair, optionally move the green Loop Start.
7. Drag or save only a result that passes the quality gate.

## Mandatory audio guarantees

- No NaN/Inf, unbounded DC, digital clipping, intersample overload or obvious boundary pop.
- The complete rendered output is evaluated circularly, including final sample back to first.
- Failed peak, closure, timbre, loudness, phase/position, stability or repeat-risk checks are blocked
  from export and DAW drag.
- Rotate & Repair preserves the selected long-form content and forward chronology except for the
  deliberate circular rotation and internal overlap.
- Texture Loop has no copied short cycle, repeated attack train, reverse artefact, retained pass-by
  trajectory or simple whole-file stretching.
- Texture Loop keeps a defined material colour and texture attributes. Exact source waveform and
  event chronology may change at higher Transform settings, while local resonances, transient
  detail, and stochastic character remain measurable.
- Matching the broad spectrum alone is insufficient. A coloured-noise result must fail local-frame
  identity and noise-collapse checks even when its long-term frequency response looks correct.

## Mandatory product guarantees

- Both modes can be sample-exact. R&R Final Length defaults to the selection and may be explicitly
  set to any duration that fits inside Source In/Out.
- Source In/Out remains fixed after generation and is a hard boundary in both modes.
- Rotate & Repair exposes one editable Loop Start because its output boundary is a rotated adjacent
  source cut; the repaired old seam sits inside the result.
- Project recall restores active audio, source, range, Loop Start, parameters and analysis evidence.
- Export and DAW drag create the same 24-bit WAV samples used for audition.
- Memory and project-state size are measured at 48 and 96 kHz.
- Windows x64 and Apple Silicon packages pass validator plus hands-on REAPER tests.

## Release evidence

Use a licensed corpus covering wind, rain, surf, fire, rooms, crowds, engines, machines, drones,
tonal beds and difficult stereo sources. Every class requires blind A/B against a competent manual
result, long-loop endurance listening, 44.1/48/96 kHz coverage, recall, export/reimport and host
automation checks.

The current 0.8.0 pre-release compiles and passes deterministic tests locally. Automatic builds are
disabled; validator, trusted-meter, corpus, blind-listening and physical Apple Silicon evidence are
still missing, so it does not yet satisfy this sale-ready contract.
