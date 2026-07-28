# macOS / Reaper test checklist

Use native Apple Silicon Reaper. Install `Loop Surgeon.vst3` under
`~/Library/Audio/Plug-Ins/VST3/`, then run **Preferences > Plug-ins > VST > Re-scan**.

## Functional test

1. Drag a WAV/AIFF/FLAC source into Loop Surgeon.
2. Move Source In/Out, select **Stationary Texture**, set 20–30 seconds and 65–80% Variation, then
   generate.
3. Confirm **Source** and **Generated** each switch the audition and immediately start it;
   **Preview/Stop** must stop and restart cleanly.
4. Compare all three generated variations and use **New Variation**.
5. Listen for copied pass-by/rise/fall trajectories, fixed electronic tones, comb filtering,
   pumping, stereo drift, and the full-file tail-to-head boundary.
6. Repeat in **Direct Seam Loop** with periodic material. Verify the green Loop In/Out markers stay
   inside blue Source In/Out, then verify **Auto** selects an appropriate path.
7. Export the 24-bit WAV and compare it with plug-in playback.
8. Save, close, and reopen the project; verify the selected generated result returns.
9. Repeat with mono/stereo audio at 44.1, 48, and 96 kHz and common buffer sizes.
10. Test the secondary **Record DAW Input** path.

Record Reaper version, macOS version, buffer size, sample rate, source file, mode, length, variation,
candidate, and a minimal project when reporting a problem.
