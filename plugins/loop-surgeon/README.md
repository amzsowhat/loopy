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
3. Raise Crush only when smaller repeated ADSR rises and dips still remain.
4. Leave Extra Off for Natural, or select Patina, Bloom or Fray and set Extra Amount.
5. Generate, audition and use New Variation when another deterministic traversal is wanted.
6. Save WAV or drag the exact-length result into the DAW.

Texture Loop uses the selected waveform itself. It removes a controllable amount of macro envelope,
chooses phase-compatible regions, avoids recently used regions and assembles them with short,
actual-overlap-aligned circular crossfades. Stereo channels always use the same traversal. No named-material classifier,
synthetic noise source, oscillator, spectral delay, pitch shift or reverse path exists.

Crush is not a conventional compressor and does not alter stereo channels independently. It applies
two circular, linked RMS-envelope correction scales after construction, then the existing loudness,
DC and true-peak safeguards run. Its 0% position is an exact bypass of the Crush stage.

Extra is a separate optional stage after Natural. Off and 0% are exact bypasses. Patina uses
memory-bearing nonlinear colour, Bloom uses source-derived circular diffusion, and Fray emphasizes
short material detail. None replaces the Natural generator.

## Evidence boundary

The local VST3 and Standalone compile and deterministic/state tests pass. These checks establish
software behavior and numeric safety, not subjective audio quality. No quality total, PASS badge,
subjective sub-score or export gate exists.

## Neural-network boundary

A neural waveform generator is not included: there is no licensed training corpus or evidence that
its deployment cost improves this task. A later small model may assist source segmentation or
parameter suggestions only after the deterministic engine has a real-SFX regression corpus.
