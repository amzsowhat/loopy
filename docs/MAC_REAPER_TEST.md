# Loop Surgeon Apple Silicon / REAPER test

No current Apple Silicon binary has been produced. When a local M-series build is available, test
Rotate & Repair first; the creative-loop replacement remains outside the plug-in until its offline
WAVs are approved.

1. Build with the macOS preset on an Apple Silicon Mac.
2. Copy `Loop Surgeon.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and re-scan in native REAPER.
3. Test import, Source In/Out, exact Final Length, audition transport, manual Loop Start, export, DAW
   drag and project restore at 44.1/48/96 kHz.
4. Report crashes, missing state, length mismatches, clicks, broken transport or channel changes with
   the source, exported WAV, sample rate and exact reproduction steps.

