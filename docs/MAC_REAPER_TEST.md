# macOS / Reaper test checklist

## Build

Requirements: macOS 11 or newer, Xcode command-line tools, CMake 3.22+, Git, and internet access.

```bash
git clone https://github.com/amzsowhat/sound-VST-project.git
cd sound-VST-project
git switch agent/loop-surgeon-demo
cmake --preset macos-debug
cmake --build --preset build-macos-debug
ctest --preset test-macos-debug
```

The preset requests a universal `arm64;x86_64` binary. On an Apple Silicon Mac, the native arm64
slice is used; Intel Reaper can use the x86_64 slice.

## Install for the current user

Copy the generated `Loop Surgeon.vst3` bundle to:

```text
~/Library/Audio/Plug-Ins/VST3/
```

Then open Reaper and use **Preferences > Plug-ins > VST > Re-scan**. For an unsigned Debug build,
macOS may require **System Settings > Privacy & Security > Open Anyway**.

## Functional test

1. Insert Loop Surgeon on a stereo audio track.
2. Keep host sync enabled and choose one bar.
3. Start transport, press Capture Input midway through a bar, and confirm capture begins on the next
   bar line.
4. Confirm the UI moves from Armed to Capturing to Analysing to Ready.
5. Listen at 100% Loop Mix and automate Mix while checking for clicks.
6. Save the Reaper project, close Reaper, reopen it, and confirm the captured loop returns.
7. Repeat at 44.1, 48, and 96 kHz with buffer sizes 32, 64, 256, and 1024.
8. Test mono and stereo tracks, Clear Loop, recapture, offline render, and project sample-rate change.

Record Reaper version, macOS version, CPU architecture, failing step, and a minimal project when
reporting a problem.
