# P2 Loop Surgeon

Current target: **0.5 Alpha texture engine**.

Loop Surgeon is a Windows/macOS VST3 effect and Standalone application for turning a user-selected
piece of audio into either a long evolving texture or a conventional repaired loop. File import is
the primary workflow; DAW-input capture is secondary.

## Processing modes

- **Evolving Texture** is the default for wind, rain, room tone, machinery, drones, and other
  reasonably stationary material. It rearranges different long grains from the selected source,
  scores transitions by level, change rate, spectrum, and stereo correlation, penalizes recently
  reused or adjacent source positions, and renders the path with circular overlap-add. The result is
  a new 4–60 second file whose internal sequence is not one short section copied on every pass.
- **Seam Loop** is for rhythmic, mechanical, tonal, or already-periodic material. It searches for a
  stable period and start/end pair, then evaluates multiple repair windows.
- **Auto** first runs periodicity and seam analysis. A strong, high-confidence period uses Seam
  Loop; other material uses Evolving Texture. The user can always override the choice.

## Workflow

1. Drop a WAV, AIFF, FLAC, or OGG file onto the plug-in, or use **Choose Audio...**.
2. Drag blue **Source In/Out** markers to select the part of the recording that may be sampled.
3. Choose **Auto**, **Evolving Texture**, or **Seam Loop**.
4. For texture mode set **Length** and **Variation**, then press **Generate Texture**. Three
   deterministic seed variations are generated on the background analysis thread.
5. **Source** and **Generated** both select the audition source and start playback immediately.
   **Preview/Stop** remains the explicit transport. Selecting a variation also starts it from the
   beginning.
6. Use **New Variation** for three new deterministic results, or export the selected result as a
   24-bit WAV.
7. The selected generated audio is embedded in DAW project state and restored with the project.

Imported source is limited to 60 seconds. Evolving Texture output is 4–60 seconds; Seam Loop output
is limited to 16 seconds.

## Evolving Texture algorithm

The texture engine is an offline, deterministic long-grain synthesizer:

1. The selected source is divided into overlapping candidate grains. Grain duration adapts between
   roughly 0.45 and 1.8 seconds to preserve recognizable environmental events and avoid the
   “blender” character of very short granular clouds.
2. Each candidate head and tail receives an RMS, derivative, stereo-correlation, and eight-band
   spectral signature.
3. A seeded path search ranks the next grain by acoustic transition cost. Variation widens the
   top-ranked choice pool while penalties discourage immediate reuse, recent reuse, near-identical
   positions, and simply continuing through the source in order.
4. The final path steps also consider the transition back to the first grain.
5. Long grains are rendered around a circular output buffer with equal-power-style overlap windows
   and per-sample normalization. Because the render is circular, export and playback use the same
   samples and need no second seam crossfade.
6. Three seeds provide alternate results. The seed is deterministic for repeatable project recall;
   **New Variation** deliberately advances it.

This is signal-processing texture synthesis, not a generative neural model. A fixed exported WAV
must eventually repeat as a whole; the improvement is that its internal 4–60 second trajectory is
recombined and evolving rather than one identical short pass repeated continuously.

## Stable parameters

| ID | Range | Default | Purpose |
| --- | --- | --- | --- |
| `loopLength` | 0.25–16 s | 4 s | Secondary DAW-capture duration |
| `syncToHost` | off/on | on | Secondary capture alignment |
| `bars` | 1/2/4/8 | 1 | Secondary capture size |
| `crossfadeMs` | 1–250 ms | 25 ms | Seam Loop repair limit |
| `mix` | 0–100% | 100% | Audition mix |
| `generationMode` | Auto/Texture/Seam | Texture | Imported-source algorithm |
| `textureDuration` | 4–60 s | 24 s | Generated texture length |
| `variation` | 0–100% | 72% | Path diversity versus closest transitions |

## Real-time and memory rules

- Decode, resampling, feature analysis, path search, texture rendering, state compression, and WAV
  writing never run on the audio thread.
- The audio thread does not allocate, lock, perform file I/O, or format log messages.
- Imported source uses windowed-sinc resampling and is bounded to 60 seconds.
- The three texture variants use buffer ownership swaps when selected, so the active result is not
  duplicated merely for audition. Previous variants are released before a new generation.
- At the maximum 60-second, 48 kHz stereo setting, source plus three float variants are roughly
  92 MB before framework and temporary feature overhead. Default 24-second output is substantially
  lower. This remains a deliberate alpha ceiling, not a claim of sampler-grade streaming memory.

## Known alpha limitations

- Listening quality has deterministic synthetic tests but not yet a sufficiently large licensed
  wind/rain/ambience corpus or blind commercial comparison. “Commercial quality” is therefore not
  claimed yet.
- Long-grain rearrangement works best on stationary or stochastic textures. Speech, melodies,
  isolated impacts, or a very short source may expose reordered events or repeated identities.
- There is no transient-class segmentation, pitch/formant-preserving variation, multi-layer
  spectral resynthesis, neural inpainting, or phase-vocoder morphing yet.
- Source In/Out has no waveform zoom, pan, typed sample entry, snapping, undo/redo, or keyboard
  nudge.
- Quality bars summarize closure, transition, spectrum, circularity, stereo, and diversity; there
  is not yet a full spectrum/phase inspector.
- Export does not yet add WAV `cue`/`smpl` chunks or a sidecar recipe.
- Project state restores the selected generated audio and parameters, not the entire source and all
  alternate variations.
- Decode/resampling and WAV writing are still synchronous UI jobs; a long file may briefly freeze
  the editor. Texture synthesis itself is on the background analysis thread.
- Apple Silicon/Reaper and Windows/Reaper still require hands-on DAW validation for this revision.
