# Loop Surgeon 0.9.0 pre-release review

## Decision

The 0.8 Texture implementation is retired. Its Continuous/Particles routing, exemplar scheduler and
event repetition model are not part of the 0.9 engine. R&R remains independent and unchanged in
purpose.

## 0.9 Texture architecture

- one source-agnostic STFT analysis model;
- macro-level normalization before material-state extraction;
- persistent spectral shape plus time-varying normalized states;
- phase-deviation analysis for phase-continuous resynthesis;
- preserved Mid/Side energy and left/right balance;
- deterministic random closed splines spanning the complete output duration;
- three explicit user styles: Organism, Spectral Drift and Fracture;
- no source sample, grain, event, reverse or time-stretch playback in the output path;
- circular true-peak ceiling and recurrence, closure, identity, noise-collapse and stereo QC.

The architecture follows the useful boundary demonstrated by established creative tools: the
processor owns a distinctive generative behavior, exposes a few high-impact controls, separates
stable body/detail concerns, and produces a repeatable result from an internal model. It does not
claim to reproduce any proprietary algorithm.

## Current evidence

- Local Debug VST3 and Standalone build successfully.
- Deterministic tests pass after replacing material-labelled regression cases with orthogonal signal
  property probes.
- Initial 48 kHz probing exposed short sub-periods and damaged stereo correlation. Integer-rate
  oscillators were replaced by full-length random closed splines; Mid/Side and channel energy are
  now retained. The corrected Organism render passes the automated gate.

## Open blockers

1. The three styles still need direct user listening. Numeric tests cannot prove the intended
   Freakshow-like character.
2. Fracture and Spectral Drift require the same 48 kHz post-correction readback as Organism.
3. Windows REAPER, VST3 Validator, trusted meters and 44.1/96 kHz remain unverified.
4. Apple Silicon compilation and REAPER testing remain unverified.
5. Temporary synthesis memory and cancellation latency at 60 seconds remain unmeasured.

Current verdict: a new algorithmic foundation exists and the old replay design is absent, but the
sound has not yet earned a commercial-quality claim.
