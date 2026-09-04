# loopy 0.14 Windows / REAPER test

1. Copy the complete `loopy.vst3` folder to
   `C:\Program Files\Common Files\VST3\`.
2. In REAPER run **Options > Preferences > Plug-ins > VST > Clear cache/re-scan**.
3. Insert loopy. Use **LOAD AUDIO**, or drag a WAV/AIFF/FLAC/OGG into the black waveform
   window. Drag the mint IN/OUT edges; hold Alt for fine movement, Shift-drag inside the selection
   to move it, or double-click to restore the full source.
4. Test **REPAIR** with Selection, a shorter Final Length and a Final Length longer than the
   selected source. Press the central
   **GENERATE** vortex, then use **LOOP START** or the curved **JOIN POSITION** slot to move the same
   repaired boundary. Select one of the three Repair Options and audition the join.
5. Test **TEXTURE LOOP**. Move the curved **MOTION** slot between Flow, Drift and Fracture, choose
   Length, then adjust Stability and Transform.
   A/B Crush at 0%, 50% and 100%; confirm it reduces small ADSR pulses without clicks, pumping,
   stereo movement or loss of the material.
6. Leave Extra Off for the Natural reference. Then test Patina, Bloom and Fray at 50% and 100%;
   confirm returning Extra to Off removes the character stage completely.
7. Use Preview/Stop, Save WAV and Drag to DAW. The three bottom actions remain visibly recessed
   until a result exists. Hovering and pressing them changes the illustrated hardware state.
8. Save and reopen the REAPER project. Confirm source, range, selected mode and generated loop return.

TEXTURE should be judged as SFX material construction. Report obvious source-event repetition,
clicks, bursts, tonal/electronic artifacts, stereo collapse or failure to preserve material identity.
