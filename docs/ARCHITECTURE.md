# Architecture

## Monorepo boundary

Each plug-in is an independent product and build target under `plugins/`. Products may share the
same JUCE version and build tooling, but they must retain independent plug-in identifiers,
parameters, state schemas, documentation, and release artifacts.

The `shared/` directory is intentionally empty at the start. Premature shared abstractions make
audio products harder to evolve. A component moves to `shared/` only after a second product uses
the same tested contract.

## Loop Surgeon signal flow

```text
Host input -> dry path -----------------------> output mixer
          -> preallocated capture workspace
          -> background automatic boundary search
          -> exact-size published loop -> seam crossfade -> loop wet path
```

The audio thread only writes the preallocated capture workspace. A worker analyzes that immutable
capture and publishes an exact-size loop while playback is paused in the `analysing` state. UI
requests are represented by atomics and applied at the beginning of an audio block. The engine
performs no file I/O, full-buffer clearing, analysis pass, lock wait, or heap allocation in
`process()`.

## State boundary

Version 1 stores automatable parameters and gzip-compressed 32-bit float loop audio in the host
project. A later production design may additionally persist captures as managed assets with hashes
and relink support instead of embedding larger audio assets in plug-in state.
