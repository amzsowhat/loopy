# Loop Surgeon 0.5 product review

## Product boundary

The product has two intentional outputs:

- **Evolving Texture** turns a user-selected one-shot or short recording into a longer stochastic
  ambience with a circular boundary.
- **Seam Loop** finds and repairs a conventional period in already-periodic material.

It is not a general keyboard sampler and does not claim neural generation.

## Implemented in 0.5

- draggable Source In/Out selection;
- Auto, Evolving Texture, and Seam Loop modes stored as stable parameters;
- deterministic 0.45–1.8 second long-grain analysis;
- head/tail level, derivative, eight-band spectrum, and stereo features;
- transition ranking with reuse, adjacency, and recent-history penalties;
- seeded top-ranked path variation and three selectable results;
- circular overlap-add rendering with normalization and no second export-only seam repair;
- 4–60 second texture target;
- Source/Generated controls that select and immediately start audition;
- explicit Preview/Stop;
- candidate buffer ownership swapping to avoid duplicating the active long result;
- generated-audio DAW state restore and 24-bit WAV export;
- deterministic tests for seed recall, seed difference, non-identical successive windows, output
  length, circular score bounds, candidate switching, Auto periodic fallback, playback, and state.

## Paid-beta blockers

1. A licensed quality corpus covering wind, rain, surf, crowds, rooms, engines, machines, drones,
   stereo ambience, impulsive contamination, speech, and very short sources.
2. Blind listening comparisons against hand edits and established texture/granular tools, with
   labels for repetition, transition audibility, phasing, pumping, image motion, and source identity.
3. Content-aware segmentation so isolated events are not cut or over-repeated.
4. A second synthesis layer or spectral-resynthesis option for sources where long-grain montage
   cannot create enough novelty.
5. Better circular-path optimization. The current final path steps consider closure, but this is
   not a globally optimal cyclic graph search.
6. Cancellation/progress for generation, decode, resampling, and export.
7. Waveform zoom/pan, optional transient/zero-crossing snapping, typed positions, keyboard nudge,
   and undo/redo.
8. Full spectrum/phase/correlation inspection and seam-solo/endurance audition.
9. Export `cue`/`smpl` metadata and a portable source/recipe option.
10. Verified Windows/Reaper and Apple Silicon/Reaper compatibility, project recall, and sample-rate
    transitions on real hosts.

## Honest quality assessment

0.5 fixes the product-definition error in 0.4: Evolving Texture no longer exports one selected
segment that simply repeats with a crossfade. It builds a longer non-sequential trajectory from the
source and closes the full trajectory as a circle.

That is a meaningful algorithmic step, but it is not yet evidence of commercial audio quality.
Long-grain montage can still reveal source identities, reordered events, phasing, or pumping. A
fixed WAV also necessarily repeats once its complete 4–60 second duration ends. These constraints
must remain visible until corpus and blind-listening results justify stronger claims.
