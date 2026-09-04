# loopy plug-in

This directory contains the complete JUCE VST3 and Standalone product. The audio engine,
editor, assets and tests remain isolated here; generated build output belongs under the
repository-level `build/` directory.

## Processing model

### LOOP

LOOP automates the conventional sound-design loop-authoring workflow while keeping the selected
material forward and in order.

1. Analyse possible continuous source spans and boundary quality.
2. Choose loop boundaries with low discontinuity risk.
3. Create a continuous join using the overlap selected for that candidate.
4. For a longer target, repeat the prepared cycle an integer number of times across the
   exact output duration so the terminal boundary returns to the same phase.

A shorter target therefore searches for material rather than blindly cropping from a fixed
edge. A longer target repeats a prepared source cycle; it does not invoke TEXTURE.

### TEXTURE

TEXTURE is a source-driven continuous-material constructor. It combines four related forms
of control: Stability reduces macro envelope and pass-by motion; Crush reduces shorter-scale
repeated dynamics; Transform controls departure from the literal source timeline; and Motion
changes the scale and continuity of region traversal. The engine chooses phase-compatible
regions across a circular source timeline, avoids recent reuse and joins them with short
overlap-aligned crossfades. Stereo channels always share the same traversal.

TEXTURE uses no synthetic noise source, oscillator, pitch shift, reversal or named-material
classifier. Crush applies linked circular RMS-envelope correction after construction. Extra
is a separate optional character stage; Off and 0% are exact bypasses.

## Stable parameter mapping

User-visible mode names are `LOOP` and `TEXTURE`. Existing IDs and integer values remain
unchanged so saved sessions and automation stay compatible.

| Mode | UI control | Stable parameter/state |
| --- | --- | --- |
| LOOP | Final Length | `repairDuration` |
| LOOP | Seam | `crossfadeMs` |
| LOOP | Audition | `mix` |
| LOOP | Loop Start / Join Position | `repairLoopStart` plus committed boundary point |
| LOOP | Options A-C | generated candidate selection |
| TEXTURE | Length | `textureDuration` |
| TEXTURE | Stability | `flatten` |
| TEXTURE | Crush | `textureCrush` |
| TEXTURE | Transform | `sourceMatch` |
| TEXTURE | Motion | `textureStructure` |
| TEXTURE | Patina / Bloom / Fray | `textureCharacter` |
| TEXTURE | Extra Mix | `textureCharacterAmount` |
| Both | Mode | `generationMode` (`0` remains LOOP, `1` remains TEXTURE) |

## Real-time and state rules

- Audio-thread code does not allocate, lock, perform file I/O or format logs.
- Analysis and rendering run away from the audio callback.
- The generated buffer and source context are stored with DAW state.
- Stereo processing remains linked.
- Final cleanup removes non-finite/DC faults and applies the circular true-peak ceiling.

## Interface

The editor is drawn in JUCE with a 1360 x 880 design space and a fixed 17:11 ratio.
Controls use shared custom geometry and typography; no stock slider text boxes, value
bubbles or bitmap background are used. The static Mobius surface identifies the output
area without continuous decorative animation.

## Build and tests

From the repository root:

```powershell
cmake --preset vs2022-debug
cmake --build --preset build-debug --config Debug
ctest --preset test-debug -C Debug --output-on-failure
```

`LoopEngineTests` covers deterministic analysis, exact-length LOOP output on both sides
of the source duration, manual-boundary state, TEXTURE determinism, bypass behavior and
numeric safety.

Compilation and automated tests do not prove subjective loop quality. Real recordings,
repeated audition and the target host remain the product-acceptance boundary.
