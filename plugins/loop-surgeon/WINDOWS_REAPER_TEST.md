# Loop Surgeon Windows / REAPER test

The current VST3 test build exposes Rotate & Repair only. Texture 0.9 is withdrawn; do not install or
test its old package.

1. Copy the locally built `Loop Surgeon.vst3` folder to
   `C:\Program Files\Common Files\VST3\`.
2. In REAPER, run **Options > Preferences > Plug-ins > VST > Clear cache/re-scan**.
3. Load a source file, set Source In/Out and choose Selection or an exact Final Length.
4. Generate, compare Source and Generated, drag Loop Start if required, then test Save WAV and Drag
   Loop to DAW.
5. Save and reopen the REAPER project. Confirm source, range, result and Loop Start return.

The separate files under `build/listening-gate/spectral-orbit/` are offline research WAVs. They are
not produced by the installed VST3 and must be judged by listening before integration.

