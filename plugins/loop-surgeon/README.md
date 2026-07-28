# P2 Loop Surgeon

Current target: **0.5.2 Alpha spectral-texture engine**.

Loop Surgeon is a Windows/macOS VST3 effect and Standalone application for turning a user-selected
piece of audio into either a long source-coloured stationary texture or a conventional repaired
short loop. File import is the primary workflow; DAW-input capture is secondary.

## Processing modes

- **Stationary Texture** is for wind, rain, room tone, machinery, drones, and other noise-like
  material. It learns the median spectral colour and Mid/Side width inside Source In/Out, then
  synthesizes a new continuous stochastic signal. It intentionally removes the source's attack,
  decay, pass-by direction, and other copied time trajectories.
- **Direct Seam Loop** is explicitly a traditional short loop. It searches only inside Source
  In/Out for a period and start/end pair, repairs the boundary, and repeats that chosen audio.
  Green Loop In/Out markers show the exact adopted segment. It does not turn a one-shot event into a
  new stationary texture.
- **Auto** keeps Direct Seam Loop only for strongly periodic, high-confidence material; otherwise
  it uses Stationary Texture. The user can override this choice.

## Workflow

1. Drop a WAV, AIFF, FLAC, or OGG file onto the plug-in, or use **Choose Audio...**.
2. Drag blue **Source In/Out** markers. This is a hard analysis boundary in every mode.
3. Choose **Auto**, **Stationary Texture**, or **Direct Seam Loop**.
4. For texture mode set **Length** and **Variation**, then press **Generate Texture**. Three
   deterministic variations are generated on the background analysis thread.
5. **Source** and **Generated** both select the audition source and start playback immediately.
   **Preview/Stop** remains the explicit transport.
6. Use **New Variation** for three new deterministic results, or export the selected result as a
   24-bit WAV.
7. The selected generated audio is embedded in DAW project state and restored with the project.

Imported source is limited to 60 seconds. Stationary Texture output is 4–60 seconds; Direct Seam
Loop output is limited to 16 seconds.

## Stationary Texture algorithm

The 0.5.2 engine replaces the correlated long-grain montage used in 0.5.0/0.5.1:

1. Resample the selected Source In/Out range to the host rate.
2. Measure up to 256 distributed analysis frames. Reject silence and extreme hit-level outliers.
3. Convert stereo input to Mid/Side and calculate a 4096-point spectral model for each active
   frame.
4. Take the per-bin median over time. This retains recurring spectral colour while discarding a
   one-shot's ordered rise, fall, or pass-by trajectory.
5. Apply light log-frequency smoothing to suppress isolated bin artefacts without replacing the
   source's broad-band tonal balance.
6. Generate deterministic, non-coherent Gaussian spectra around the learned Mid/Side models.
   Independent random phase prevents the comb filtering and fixed electronic tones caused by
   overlapping correlated source grains.
7. Add bounded, slowly varying broad-band drift controlled by **Variation**. This supplies natural
   movement without replaying a source event.
8. Inverse-transform sine-windowed frames into the requested circular buffer with per-sample energy
   normalization. Frames wrap through the buffer boundary, so the boundary has ordinary adjacent
   noise continuity rather than a special crossfade event.
9. Match the active source RMS, preserve Mid/Side energy, cap peaks, and report closure,
   stationarity, timbre, stereo, and macro-stability scores.

The output is signal-processing texture synthesis, not a generative neural model. A fixed exported
WAV repeats after its full 4–60 second length, but it contains no copied short pass and no preserved
source-event timeline.

## Stable parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `loopLength` | 0.25–16 s | 4 s | Secondary DAW-capture duration |
| `syncToHost` | off/on | on | Secondary capture alignment |
| `bars` | 1/2/4/8 | 1 | Secondary capture size |
| `crossfadeMs` | 1–250 ms | 25 ms | Direct Seam Loop repair limit |
| `mix` | 0–100% | 100% | Audition mix |
| `generationMode` | Auto/Texture/Seam | Texture | Imported-source algorithm |
| `textureDuration` | 4–60 s | 24 s | Generated texture length |
| `variation` | 0–100% | 72% | Bounded spectral drift amount |

## Real-time and memory rules

- Decode, resampling, FFT analysis, synthesis, state compression, and WAV writing never run on the
  audio thread.
- The audio thread does not allocate, lock, perform file I/O, or format log messages.
- Imported source uses windowed-sinc resampling and is bounded to 60 seconds.
- Analysis uses at most 256 source frames. Temporary spectral models are bounded independently of
  source duration.
- The three long variants use buffer ownership swaps when selected; the active result is not
  duplicated merely for audition. Previous variants are released before new generation.
- At the maximum 60-second, 48 kHz stereo setting, source plus three float variants are roughly
  92 MB before framework and bounded FFT/model workspaces. This remains an alpha ceiling rather
  than sampler-grade streaming.

## Known alpha limitations

- The supplied whoosh/pass-by regression and deterministic synthetic corpus now cover copied
  attacks, repeated spectral trajectories, circular closure, stereo width, and recall. A larger
  licensed wind/rain/ambience corpus and blind commercial comparison are still required before
  claiming commercial quality.
- Stationary Texture deliberately removes event identity. It is inappropriate when the desired
  result must preserve speech, melody, a recognizable impact, pitch motion, or the original
  direction of travel; use Direct Seam Loop or another product for those cases.
- Median spectral modelling preserves recurring colour, not the original short-time phase.
  Extremely tonal material can sound more noise-like, and very short sources provide a weaker
  statistical estimate.
- The model preserves global Mid/Side energy but does not yet reproduce frequency-dependent stereo
  coherence or moving spatial trajectories.
- Source In/Out has no waveform zoom, pan, typed sample entry, snapping, undo/redo, or keyboard
  nudge.
- Export does not yet add WAV `cue`/`smpl` chunks or a sidecar recipe.
- Project state restores the selected generated audio and parameters, not the entire source and all
  alternate variations.
- Decode/resampling and WAV writing are still synchronous UI jobs; a long file may briefly freeze
  the editor. Texture synthesis itself runs on the background analysis thread.
- Apple Silicon/Reaper and Windows/Reaper still require hands-on DAW validation for this revision.
