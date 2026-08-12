# Sound VST Project

Monorepo for independent audio plug-ins. Each product stays under `plugins/<product-slug>/`.

## Current product

| Product | Directory | Current local state |
| --- | --- | --- |
| P2 Loop Surgeon | `plugins/loop-surgeon` | Rotate & Repair and Texture Loop are both available in the VST3 |

Loop Surgeon builds as a Windows VST3 and Standalone application. Both modes use the waveform range
between Source In and Source Out and create an exact user-defined output length.

- **Rotate & Repair** automates the conventional rotate, overlap and seam-repair workflow.
- **Texture Loop** removes macro one-shot movement and constructs a circular long-form SFX texture
  from phase-coherent regions of the selected waveform. It does not add oscillators, synthetic
  noise, pitch shifting, spectral delay, tempo-locked movement or reverse playback.

Generated audio is stored in DAW state and can be saved as 24-bit WAV or dragged to the DAW.

## Local Windows build

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

GitHub Actions are not used while the account quota is exhausted. Builds and tests are local only.

## Licensing

JUCE is dual-licensed. Verify the current JUCE terms before selling proprietary binaries.
