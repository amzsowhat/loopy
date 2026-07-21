# Windows / Reaper smoke test

## Install

1. Download `Loop-Surgeon-Windows-x64-VST3.zip` from the `v0.2.0-alpha` release.
2. Extract the complete `Loop Surgeon.vst3` directory to
   `C:\Program Files\Common Files\VST3\`.
3. Start the 64-bit build of Reaper.
4. Open **Options > Preferences > Plug-ins > VST** and run **Clear cache/re-scan**.
5. Search the FX browser for `Loop Surgeon`.

## Functional test

1. Insert Loop Surgeon on a mono or stereo audio track.
2. Start playback, keep host sync enabled, select one bar, and press **Capture Input**.
3. Confirm the status moves through Armed, Capturing, Analysing, and Ready.
4. Listen at 100% Loop Mix and automate the Mix control while checking for clicks.
5. Save the Reaper project, close Reaper, reopen it, and confirm the loop returns.
6. Test Clear Loop, recapture, offline render, mono/stereo tracks, and 44.1/48/96 kHz projects.

If Reaper cannot scan the plug-in, confirm that Reaper is 64-bit and install the current Microsoft
Visual C++ 2015-2022 x64 Redistributable before rescanning.
