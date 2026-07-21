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
          -> capture buffer -> seam crossfade -> loop wet path
```

The audio thread owns all loop-buffer mutation. UI requests are represented by atomics and applied
at the beginning of an audio block. The demo performs no file I/O or heap allocation in `process()`.

## State boundary

Version 0 stores automatable parameters in the host project. Captured audio is deliberately not
serialized yet. A production design should persist captures as managed assets with hashes and
relink support instead of silently embedding unbounded audio in plug-in state.

