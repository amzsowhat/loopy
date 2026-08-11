# Sound VST Project

Monorepo for independent audio plug-ins. Each product stays under `plugins/<product-slug>/`.

## Current product

| Product | Directory | Current state |
| --- | --- | --- |
| P2 Loop Surgeon | `plugins/loop-surgeon` | R&R works locally; creative Texture mode is withdrawn and rebuilding |

Loop Surgeon currently builds as a Windows VST3 and Standalone application. Rotate & Repair accepts
an audio file or DAW capture, respects Source In/Out, creates an exact-length repaired loop, stores
the result in DAW state and exports a 24-bit WAV.

The rejected Texture 0.9 package is not a test candidate. Its random spectral-state resynthesis
produced unacceptable industrial/noise-like output. The UI option is disabled and the processor
forces R&R while a replacement is evaluated in `plugins/loop-surgeon/Research/` as an offline WAV
renderer. Research output is not part of the VST3 until it passes a human listening gate.

## Local Windows build

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

GitHub Actions are not used while the account quota is exhausted. Builds and tests are local only.

## Repository layout

```text
plugins/                 Independent product directories
shared/                  Code used by at least two products
docs/                    Cross-product architecture and roadmap
.github/workflows/       Manual workflows only; do not run while quota is exhausted
```

## Licensing

JUCE is dual-licensed. Verify the current JUCE terms before selling proprietary binaries.

