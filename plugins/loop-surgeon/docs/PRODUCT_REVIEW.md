# Loop Surgeon product review

## Product boundary

Loop Surgeon is not a general sampler instrument. It accepts a user-selected section of source
audio, automatically proposes and repairs seamless loops, lets the user verify or refine the result,
and exports a reusable loop. Features that do not shorten that path are secondary.

## Required before a paid beta

### Implemented in the current alpha

- drag/drop audio import and bounded source memory;
- draggable Source In/Out selection with selection-only reanalysis;
- draggable final Loop In/Out with immediate fixed-range evidence recalculation;
- automatic period proposals using loudness, change-rate, and multi-band feature correlation;
- whole-selection in/out pair search and single-sample finalist refinement;
- direct waveform-jump, level, slope, phase, spectrum, stereo, and transient evidence;
- three selectable candidates and explicit low-confidence failure;
- Original/Loop A/B playback using the imported source;
- phase-aware repair shared by real-time preview and WAV export;
- overlap-length compensation so repair does not shorten the detected output period;
- 24-bit WAV export and finished-loop DAW state restore.

### Still mandatory

1. **Precision editing around the implemented Loop In/Out**: typed time/sample values, keyboard
   nudging, linked movement, and undo/redo.
2. **Waveform navigation**: horizontal zoom/pan, overview, and a legible sample/time ruler.
3. **Snap modes**: off, zero crossing, transient, and host grid. Snapping must be optional because the
   automatic optimum may intentionally sit away from a zero crossing when a crossfade is superior.
4. **Seam audition**: Original/Loop level-matched A/B, seam-only repeat, and 10/30/100-repeat endurance.
5. **Visible repair region**: draggable crossfade length and curve with transient-protection warning.
6. **Export contract**: WAV plus `cue`/`smpl` loop chunks and sidecar JSON; exported audio must null or
   closely match the plug-in preview.
7. **Complete project recall**: source reference/hash, Source In/Out, candidate list, selected result,
   manual edits, repair settings, and a portable "collect source" option.
8. **Non-blocking jobs**: decode, production-grade band-limited sample-rate conversion, analysis, and
   export on cancellable workers with progress and deterministic cancellation.
9. **Quality corpus**: licensed ambience, rain, wind, engines, machines, tonal beds, stereo textures,
   and adversarial non-loopable material; objective boundary metrics plus blind listening labels.

## Important but not beta-blocking

- event-map detection and constrained rearrangement to reduce long-term repetition;
- multiple rendered variants and long-timeline export;
- optional M/S and local STFT repair for candidates that time-domain repair cannot solve;
- tempo/BPM inference and fixed musical-length search;
- batch processing and standalone queue.

## Explicit non-goals

- eight-layer instrument sampling, keyboard mapping, synthesis, filters, modulation, or time-stretch
  sound design in the style of CR8;
- claiming every source is loopable;
- hiding a low-confidence result behind a polished score or marketing copy.
