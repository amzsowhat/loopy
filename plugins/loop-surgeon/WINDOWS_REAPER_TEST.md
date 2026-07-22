# Windows / Reaper smoke test

## Install

1. Download `Loop-Surgeon-Windows-x64-VST3.zip` from the `v0.3.0-alpha` release.
2. Extract the complete `Loop Surgeon.vst3` directory to
   `C:\Program Files\Common Files\VST3\`.
3. Start the 64-bit build of Reaper.
4. Open **Options > Preferences > Plug-ins > VST** and run **Clear cache/re-scan**.
5. Search the FX browser for `Loop Surgeon`.

## Functional test

1. Insert Loop Surgeon on a mono or stereo audio track and drag in a WAV/AIFF/FLAC source.
2. Drag blue Source In/Out, run **Analyze Selection**, and confirm the green result stays inside it.
3. Switch among all candidates and compare **Original** with **Loop**.
4. Drag green Loop In/Out and adjust **Seam repair** while checking for clicks and level bumps.
5. Export the WAV, re-import it on a track, and compare repeated playback with the plug-in preview.
6. Save the Reaper project, close Reaper, reopen it, and confirm the finished loop returns.
7. Repeat with mono/stereo material at 44.1/48/96 kHz. Test the secondary **Use DAW Input** path too.

If Reaper cannot scan the plug-in, confirm that Reaper is 64-bit and install the current Microsoft
Visual C++ 2015-2022 x64 Redistributable before rescanning.
