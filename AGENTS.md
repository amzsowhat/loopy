# Codex repository guide

This repository contains multiple independent audio plug-ins. Keep each product isolated under
`plugins/<product-slug>/`. Put code in `shared/` only after at least two products genuinely use it.

## Current product

- `plugins/loop-surgeon/`: P2 Loop Surgeon VST3 and Standalone demo.

## Working rules

1. Do not place product source files in the repository root.
2. Audio-thread code must not allocate, lock, perform file I/O, or format log messages.
3. Preserve stable parameter IDs after a product has been released.
4. Keep generated files under `build/`; never commit plug-in binaries or JUCE build output.
5. Add or update tests for deterministic DSP and state changes.
6. Document unfinished product behavior in the product README instead of hiding it.

## Common commands

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

The project fetches the pinned JUCE release during CMake configure. A commercial JUCE licence may
be required before distributing proprietary binaries; verify licensing before any public release.

