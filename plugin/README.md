# P2 loopy

loopy is one VST3 with two equally available workflows.

## Interface

The editor is rebuilt in JUCE drawing code with a 1360 x 880 design space and
a fixed 17:11 resize ratio. No background, knob, track or fog PNG is loaded.
Only the two bundled typefaces are embedded by the UI asset target.

The rectangular workspace uses one charcoal/warm-neutral/copper palette:
source and waveform at upper left, four compact parameter controls below it,
a horizontal position/motion control beneath, and generation plus result
selection on the right. Preview and delivery share the bottom action row.
A static mathematical Mobius surface identifies the output area. Generate has
no rotating image layers, fog or continuous animation.

### UI implementation contract

- Colours and design dimensions are defined in `Source/UiTheme.h`.
- Visible controls are custom JUCE drawing code, with shared type and state
  treatments. No stock slider text boxes or native value bubbles are shown.
- Slider track drawing and mouse mapping use the same geometry.
- Mode-specific bounds and visibility update together on a mode change.
- Waveform range editing stays within a rectangular inset; transient readouts
  use the waveform's own lower area.
- Audio parameters, automation IDs, generation and export logic are unchanged.

### Current verification and remaining acceptance

Debug VST3/Standalone compilation and actual empty-source layout inspection
cover both modes. The user owns real-audio testing, REAPER verification and
subjective visual acceptance. No claim of host/audio acceptance is made.
Direct numeric typing and the Source/Result comparison selector are not exposed
by this layout; their DSP/state support is retained.

### Control to parameter mapping

| Mode | UI control | Parameter/state |
| --- | --- | --- |
| Rotate & Repair | Final Length | `repairDuration` |
| Rotate & Repair | Seam | `crossfadeMs` |
| Rotate & Repair | Audition | `mix` |
| Rotate & Repair | Loop Start / Join Position / LOOP marker | `repairLoopStart` plus committed manual rotation point |
| Rotate & Repair | Repair Options A-C | generated candidate selection |
| Texture Loop | Length | `textureDuration` |
| Texture Loop | Stability | `flatten` |
| Texture Loop | Crush | `textureCrush` |
| Texture Loop | Transform | `sourceMatch` |
| Texture Loop | Motion | `textureStructure` (`Flow`, `Drift`, `Fracture`) |
| Texture Loop | Patina / Bloom / Fray | `textureCharacter` |
| Texture Loop | Extra Mix | `textureCharacterAmount` |

## Rotate & Repair

1. Load or capture audio and set Source In/Out.
2. Set Final Length to Selection or a shorter duration.
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
