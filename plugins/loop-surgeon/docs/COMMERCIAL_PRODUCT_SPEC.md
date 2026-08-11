# Loop Surgeon product contract

Loop Surgeon will contain two equally important user-selected modes.

## Rotate & Repair

Automate the standard long-loop edit while preserving the selected timeline and an originally
adjacent exported boundary. The user controls Source In/Out, final length, repair overlap and manual
Loop Start.

## Creative Loop — rebuilding

Turn input into a designed, exact-length loop with a recognisable algorithmic character. The user
must be able to shape the result through an editable frequency-to-delay curve, feedback, diffusion,
wrap/Barberpole behavior, stereo link, output length and wet/dry. Regular source replay, generic
noise fallback and uncommanded clipping are unacceptable.

The previous Texture 0.9 implementation is excluded from this contract. The replacement must first
pass a human listening gate as an offline WAV renderer before it is integrated into the VST3.

## Evidence policy

- Numeric checks may report finite samples, DC, true peak, file duration, channel count and boundary
  values.
- Internal R&R seam math may rank candidates but is not shown as an audio-quality grade.
- No automated total or threshold may claim that a sound is useful, commercial or approved.

