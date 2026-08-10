# macOS / REAPER test checklist

Use native Apple Silicon REAPER. Install `Loop Surgeon.vst3` under
`~/Library/Audio/Plug-Ins/VST3/`, then run **Preferences > Plug-ins > VST > Re-scan**. The current
0.6.0 source has no new package yet; do not use an older binary to judge these changes.

## Functional test

1. Drag a WAV/AIFF/FLAC source into Loop Surgeon and confirm blue Source In/Out stays fixed after
   generation.
2. In **Rotate & Repair**, use a long intended ambience loop with a bad original head/tail. Confirm
   the result keeps the selected long material, the green Loop Start is draggable, and neither the
   internal repair nor full-buffer wrap is audible.
3. In **Texture Loop**, use a one-shot/pass-by source. Set exact Output Length, then compare low/high
   Flatten and Source Match values.
4. Confirm Source and Generated start the chosen audition; Preview/Stop must stop and restart
   cleanly.
5. Compare both retained candidates and use New Variation.
6. Inspect spectrum overlay, phase scope, correlation and position. Listen for retained ADSR or
   pass-by, copied short cycles, multiple attacks, fixed electronic tones, comb filtering, pumping,
   channel drift and the full-file tail-to-head boundary.
7. Drag the result to REAPER and separately Save WAV; both must match plug-in playback.
8. Save, close and reopen the project. Confirm active output, source, Source In/Out, Loop Start and
   parameters return.
9. Repeat mono/stereo at 44.1, 48 and 96 kHz and common buffer sizes.
10. Test secondary Record DAW Input in both modes.

Record REAPER/macOS versions, buffer, sample rate, source, mode, all mode parameters, candidate and
a minimal project when reporting a problem.
