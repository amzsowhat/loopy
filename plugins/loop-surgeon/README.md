# P2 Loop Surgeon

Loop Surgeon is one VST3 with two equally available workflows.

## Rotate & Repair

1. Load or capture audio and set Source In/Out.
2. Set Final Length to Selection or type an exact shorter duration.
3. Generate, compare Source and Generated, and move Loop Start if required.
4. Save WAV, drag the loop into the DAW, or save it with the DAW project.

The source remains forward and in order. The engine rotates an adjacent internal cut to the exported
boundary and repairs the moved original head/tail join.

## Texture Loop

1. Define the source material with Source In/Out.
2. Select Flow, Drift or Fracture, set Output Length, Stability and Transform.
3. Generate, audition and use New Variation when another deterministic traversal is wanted.
4. Save WAV or drag the exact-length result into the DAW.

Texture Loop uses the selected waveform itself. It removes a controllable amount of macro envelope,
chooses phase-compatible regions, avoids recently used regions and assembles them into a circular
overlap-add buffer. Stereo channels always use the same traversal. No named-material classifier,
synthetic noise source, oscillator, spectral delay, pitch shift or reverse path exists.

## Evidence boundary

The local VST3 and Standalone compile and deterministic/state tests pass. These checks establish
software behavior and numeric safety, not subjective audio quality. No quality total, PASS badge,
subjective sub-score or export gate exists.

