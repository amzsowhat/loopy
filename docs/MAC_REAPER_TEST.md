# macOS / Reaper test checklist

Use native Apple Silicon Reaper. Install `Loop Surgeon.vst3` under
`~/Library/Audio/Plug-Ins/VST3/`, then run **Preferences > Plug-ins > VST > Re-scan**.

## Functional test

1. Drag a WAV/AIFF/FLAC source into Loop Surgeon.
2. Move blue Search In/Out, click **Find Best Loop**, and confirm every candidate remains inside.
3. Start and stop **Preview**, then compare **Original** and **Loop**.
4. Drag green Loop In/Out, click **Use Manual Loop**, and confirm the markers do not reset.
5. Adjust **Max Repair Window**, re-run Find/Use Manual, and listen for clicks, flams, image jumps,
   or bumps while watching Repair/Spectrum/Phase/Stereo/Transient quality bars.
6. Export the 24-bit WAV and compare repeated playback with the plug-in preview.
7. Save, close, and reopen the Reaper project; verify the finished loop returns.
8. Repeat with mono/stereo material at 44.1, 48, and 96 kHz and common buffer sizes.
9. Test the secondary **Record DAW Input** path and confirm it does not select uncaptured silence.

Record Reaper version, macOS version, buffer size, sample rate, source file, failing candidate, and a
minimal project when reporting a problem.
