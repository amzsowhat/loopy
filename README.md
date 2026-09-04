# loopy

loopy is a JUCE audio-loop plug-in with VST3 and Standalone targets. The product source lives in
`plugin/`; this repository is dedicated to loopy.

Both modes use the waveform range
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

## Licensing

JUCE is dual-licensed. Verify the current JUCE terms before selling proprietary binaries.
The bundled Space Grotesk fonts use the SIL Open Font License included in `plugin/Assets/`.
