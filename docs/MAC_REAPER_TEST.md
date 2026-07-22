# macOS / Reaper test checklist

Use native Apple Silicon Reaper. Install `Loop Surgeon.vst3` under
`~/Library/Audio/Plug-Ins/VST3/`, then run **Preferences > Plug-ins > VST > Re-scan**.

## Functional test

1. Drag a WAV/AIFF/FLAC source into Loop Surgeon.
2. Move blue Source In/Out, click **Analyze Selection**, and confirm every candidate remains inside.
3. Compare **Original** and **Loop** at matched preview level.
4. Drag green Loop In/Out, adjust **Seam repair**, and listen for clicks, flams, image jumps, or bumps.
5. Export the 24-bit WAV and compare repeated playback with the plug-in preview.
6. Save, close, and reopen the Reaper project; verify the finished loop returns.
7. Repeat with mono/stereo material at 44.1, 48, and 96 kHz and common buffer sizes.
8. Test the secondary **Use DAW Input** capture path and confirm it does not select uncaptured silence.

Record Reaper version, macOS version, buffer size, sample rate, source file, failing candidate, and a
minimal project when reporting a problem.
