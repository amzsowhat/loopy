# loopy product review

## Implemented locally

- Two selectable VST3 modes: Rotate & Repair and Texture Loop.
- Source In/Out, exact output length, preview/stop, Source/Generated comparison, WAV export, DAW drag
  and DAW-state recall.
- Texture Loop uses circular material-flow construction from the selected waveform. The prior
  spectral/noise resynthesis and separate spectral-delay experiment are removed.
- Natural Texture now uses short complementary crossfades scored across the actual overlap, plus
  sample-scale source-position refinement. This removes the former long multi-layer overlap that
  could create comb filtering and beating.
- Texture Crush is a post-construction, circular and stereo-linked local-envelope control. At 0%
  the Crush stage is an exact bypass.
- Extra is optional and downstream of Natural: Off, Patina, Bloom and Fray, with an independent
  amount. Off and 0% are exact bypasses.
- The demo UI now follows source, range/shape, prominent Generate, result/audition and export order;
  control text, diagnostics and buttons use a larger, consistent type scale.
- Spectrum, phase plot, stereo correlation, position and true peak are diagnostics, not ratings.

## Not yet verified

- Subjective usefulness and material identity across a broad listening corpus.
- Current REAPER host behavior with the new Texture algorithm beyond local engine tests.
- Subjective Crush behaviour on real SFX, especially pumping and excessive material flattening.
- Subjective usefulness of each Extra style across varied SFX; implementation does not prove taste.
- Apple Silicon build, signing, installer, licence and commercial release validation.

The current package is a local test build, not a sale-ready binary.
