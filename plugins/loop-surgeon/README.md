# P2 Loop Surgeon

Current package target: **0.3 Alpha**.

Loop Surgeon is a VST3 effect and Standalone application that turns an existing audio material into
a seamless loop. File import is the primary workflow; DAW-input capture remains a secondary source.

## Current workflow

1. Drop a WAV, AIFF, FLAC, or OGG file onto the plug-in, or press **Import Audio**.
2. Blue Source In/Out handles limit the material the user wants sampled; **Analyze Selection**
   re-runs the automatic search only inside that range. Green Loop In/Out handles show the automatic
   result and can be dragged for an exact manual result without repeating the broad search.
3. The background analyser estimates repeating periods from loudness, change-rate, and multi-band
   spectral-feature autocorrelation.
4. For the strongest periods it searches start/end pairs across the source and scores direct sample
   jump, boundary
   level, waveform slope, short-time phase, spectrum, stereo correlation, and transient continuity.
5. Broad candidates are refined to single-sample resolution. The best candidate becomes the
   automatic preview and two alternatives remain selectable. A low-confidence warning is shown when the
   source does not contain sufficiently repeatable evidence.
6. **Original** and **Loop** provide a real imported-source A/B preview.
7. **Export Loop WAV** renders a 24-bit WAV with sample-accurate boundaries and a phase-aware
   linear/equal-power overlap at the seam. Search compensates for overlap length so the rendered loop
   keeps the detected period instead of drifting shorter on every repeat.
8. The analysed loop is embedded in the DAW project state and is restored with the project.

Imported audio is limited to the first 60 seconds in this alpha. A generated loop can be at most
16 seconds long.

## Why the algorithm is not spectrum-only

Spectral similarity alone can join two places with similar tone but incompatible waveform phase,
causing a click. Loop Surgeon therefore uses a coarse-to-fine search:

- normalized loudness/change-rate/multi-band feature autocorrelation proposes likely periods;
- the whole source is searched for in/out pairs near each period;
- cheap time-domain metrics prune the search before detailed spectral comparison;
- direct last-to-first sample jump, phase, stereo image, transient shape, level, and slope contribute
  independent seam evidence;
- the broad millisecond grid is refined around finalists one sample at a time;
- high-phase-agreement seams use a linear overlap to avoid a correlated +3 dB bump; less-correlated
  seams use an equal-power overlap. Real-time preview and WAV export now use the same fade rule.

The deterministic tests cover automatic recovery of a known period, normalized evidence scores,
rendered-boundary jump limits, real-time playback, and DAW state round-trip.

## Stable parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `loopLength` | 0.25-16 s | 4 s | Secondary DAW-capture duration |
| `syncToHost` | off/on | on | Secondary capture alignment |
| `bars` | 1/2/4/8 | 1 | Secondary capture size |
| `crossfadeMs` | 1-250 ms | 25 ms | Seam-repair window |
| `mix` | 0-100% | 100% | Dry/loop preview balance |

## Real-time and memory rules

- Audio-file decoding, resampling, period search, spectral analysis, and WAV writing do not run on
  the audio thread.
- The audio thread does not allocate, lock, perform file I/O, or format log messages.
- Imported source audio is resampled to the current host rate and bounded to 60 seconds; the
  published loop buffer contains only the selected material.
- Parameters and gzip-compressed 32-bit float loop audio are embedded in host project state.

## Known alpha limitations

- Source In/Out and Loop In/Out are draggable and reported in seconds, but typed sample-index editing,
  keyboard nudging, zoom/pan, zero-crossing/transient snapping, crop, and undo/redo are not complete.
- The UI reports spectral, phase, stereo, transient, waveform-jump, and period evidence numerically;
  a detailed spectrum view and seam-solo/10/30/100-repeat controls are not complete.
- Automatic period estimation currently uses envelope autocorrelation plus seam evidence, not a
  trained perceptual model. Non-periodic speech or one-shot material may correctly report low
  confidence rather than promise a convincing loop.
- Export writes loop audio but does not yet add `cue`/`smpl` chunks or a sidecar document.
- State restores the finished loop, not the entire imported source and alternate candidates.
- File decode/resampling and WAV writing avoid the audio thread but currently run synchronously from
  the UI callback; long files can briefly freeze the editor. Linear sample-rate conversion also needs
  replacement with a production-grade band-limited converter.
- Apple Silicon/Reaper and Windows/Reaper still require hands-on DAW validation for this revision.

