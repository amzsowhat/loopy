# Loop Surgeon roadmap

## Current local test scope

- Rotate & Repair and Texture Loop are both active in VST3 and Standalone.
- Source In/Out and exact output length apply to both modes.
- Generated audio supports preview, WAV export, DAW drag and DAW-state recall.
- No subjective quality score or export gate exists.

## Required before sale

1. Listening tests across a deliberately varied, unnamed regression corpus.
2. Fix any repeated-event, click, burst, tonal/electronic, stereo or material-loss failures at the
   algorithm level rather than adding per-material branches.
3. Validate VST3 in current REAPER and other target hosts on Windows and Apple Silicon macOS.
4. Add release signing, installer/uninstaller, licence compliance and a reproducible local release
   process. GitHub Actions remain manual and must not be triggered while quota is unavailable.
