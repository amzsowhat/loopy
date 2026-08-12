# Loop Surgeon Windows / REAPER test

1. Copy the complete `Loop Surgeon.vst3` folder to
   `C:\Program Files\Common Files\VST3\`.
2. In REAPER run **Options > Preferences > Plug-ins > VST > Clear cache/re-scan**.
3. Insert Loop Surgeon, load audio and set Source In/Out.
4. Test **Rotate & Repair** with Selection and an explicit Final Length.
5. Test **Texture Loop** with Flow first, choose Output Length, then adjust Stability and Transform.
   A/B Crush at 0%, 50% and 100%; confirm it reduces small ADSR pulses without clicks, pumping,
   stereo movement or loss of the material.
6. Leave Extra Off for the Natural reference. Then test Patina, Bloom and Fray at 50% and 100%;
   confirm returning Extra to Off removes the character stage completely.
7. Use Preview/Stop, Source/Generated, New Variation, Save WAV and Drag Loop to DAW.
8. Save and reopen the REAPER project. Confirm source, range, selected mode and generated loop return.

Texture Loop should be judged as SFX material construction. Report obvious source-event repetition,
clicks, bursts, tonal/electronic artifacts, stereo collapse or failure to preserve material identity.
