# Loop Surgeon interface assets

The 0.14 interface separates functional UI from illustration. No bitmap contains live controls,
labels, waveform data, button holes or layout sockets. JUCE draws and positions every interactive
component, so mode changes never leave decorative controls behind and rotary centres are derived
from component geometry.

## Original raster assets

`splice-ribbon-functional.png` is an original AI-assisted functional illustration made for Loop Surgeon. Its closed,
three-times-repaired Mobius path is the product-specific visual metaphor for selecting, cutting,
repairing and circulating audio. It is not copied from another plug-in and deliberately excludes
faces, creatures, eyes, hands, hardware, text and controls.

Final built-in image prompt (summarised): isolated inanimate non-Euclidean cut-and-splice ribbon;
wide Mobius loop with three repaired joints and impossible folds; contemporary underground
editorial cartoon; flat mint, coral, black, ultraviolet and ivory fills; thick irregular ink;
uniform magenta key; no UI, text, logos, vintage hardware, 3D, biological forms or detached
decorations. The built-in image generation tool produced the keyed source, then the installed
chroma-key helper converted it to this alpha PNG.

## Font

Space Grotesk Medium and Bold are bundled under the SIL Open Font License 1.1. See
`SpaceGrotesk-OFL.txt`. Functional labels use this single readable family; the illustration does
not contain generated text.

`splice-ribbon-functional.png` is the shipping functional illustration. The waveform cavity, four
parameter sockets, central Generate hub and three output tails are part of the same continuous
ribbon. JUCE adds only live waveform data, indicators, values, focus and hit targets. Rejected
generic frames, chassis and detached button skins are intentionally excluded.
