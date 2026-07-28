# P2 Loop Surgeon

Current package target: **0.4 Alpha quality preview**.

Loop Surgeon is a VST3 effect and Standalone application that turns an existing audio material into
a seamless loop. File import is the primary workflow; DAW-input capture remains a secondary source.

## Current workflow

1. Drop a WAV, AIFF, FLAC, or OGG file onto the plug-in, or press **Choose Audio...**.
2. Blue Search In/Out handles limit the material the user wants sampled; **Find Best Loop**
   searches only inside that range. Green Loop In/Out handles show the automatic result. Dragging
   them changes the primary action to **Use Manual Loop**, which retains the user's boundaries
   instead of returning to an automatic result.
3. The background analyser estimates repeating periods from loudness, change-rate, and multi-band
   spectral-feature autocorrelation.
4. For the strongest periods it searches start/end pairs across the source and scores direct sample
   jump, boundary
   level, waveform slope, short-time phase, spectrum, stereo correlation, and transient continuity.
5. Detailed finalists use short/long windows, a 16-band spectral signature, post-render repair
   simulation, source-activity rejection, and a weak-link penalty before single-sample refinement.
   The best candidate and two diverse alternatives remain selectable. Low-confidence material is
   reported explicitly.
6. Preview is stopped by default. **Preview/Stop** provides explicit transport, while **Original**
   and **Loop** switch the audition source.
7. **Export Loop WAV** renders a 24-bit WAV with sample-accurate boundaries and an automatically
   selected phase-aware repair window. Preview starts on the same repaired sample as export, and
   overlap compensation preserves the detected period.
8. The analysed loop is embedded in the DAW project state and is restored with the project.

Imported audio is limited to the first 60 seconds in this alpha. A generated loop can be at most
16 seconds long.

## Why the algorithm is not spectrum-only

Spectral similarity alone can join two places with similar tone but incompatible waveform phase,
causing a click. Loop Surgeon therefore uses a coarse-to-fine search:

- normalized loudness/change-rate/multi-band feature autocorrelation proposes likely periods;
- the whole source is searched for in/out pairs near each period;
- cheap time-domain metrics prune the search before multi-scale, 16-band spectral comparison;
- direct last-to-first sample jump, phase, stereo image, transient shape, level, and slope contribute
  independent seam evidence;
- the broad millisecond grid is refined around finalists one sample at a time;
- the analyser tests multiple repair lengths up to the user's maximum and scores the actual rendered
  boundary; high-phase seams use a linear overlap and less-correlated seams use equal power;
- severe waveform, phase, transient, or repaired-boundary defects receive a weak-link penalty, and
  silent regions cannot win with misleading perfect similarity;
- real-time preview and WAV export begin on the same repaired samples.

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
- Imported source audio uses high-order windowed-sinc resampling to the current host rate and is
  bounded to 60 seconds; the
  published loop buffer contains only the selected material.
- Parameters and gzip-compressed 32-bit float loop audio are embedded in host project state.

## Known alpha limitations

- Search In/Out and Loop In/Out are draggable and reported in seconds, but typed sample-index editing,
  keyboard nudging, zoom/pan, zero-crossing/transient snapping, crop, and undo/redo are not complete.
- The UI exposes Repair, Spectrum, Phase, Stereo, Transient, and overall quality bars; an actual
  spectrum curve and seam-solo/10/30/100-repeat controls are not complete.
- Automatic period estimation currently uses envelope autocorrelation plus seam evidence, not a
  trained perceptual model. Non-periodic speech or one-shot material may correctly report low
  confidence rather than promise a convincing loop.
- Export writes loop audio but does not yet add `cue`/`smpl` chunks or a sidecar document.
- State restores the finished loop, not the entire imported source and alternate candidates.
- File decode/resampling and WAV writing avoid the audio thread but currently run synchronously from
  the UI callback; long files can briefly freeze the editor.
- Apple Silicon/Reaper and Windows/Reaper still require hands-on DAW validation for this revision.
