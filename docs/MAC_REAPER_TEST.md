# loopy macOS / REAPER test

An Apple Silicon build has not been produced on this Windows machine. After a local arm64 macOS
build, copy `loopy.vst3` to `~/Library/Audio/Plug-Ins/VST3/`, rescan in REAPER and test both
REPAIR and TEXTURE using the same workflow as the Windows guide.

Verify exact output length, preview/stop, Source/Generated comparison, DAW save/reopen, WAV export
and DAW drag. Texture reports should identify audible repetition, clicks, tonal/electronic artifacts,
stereo change or material-identity loss; no automated metric substitutes for listening.
