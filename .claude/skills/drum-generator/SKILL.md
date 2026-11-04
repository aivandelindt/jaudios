---
name: drum-generator
description: Genre drum patterns with optional kicks/transitions, sample-based playback, export full or per-element.
---
## Steps
1. Pattern library (JSON): per genre, 1/16 grids for kick/snare/clap/hat/open-hat/perc/ride, 2–8 bars, fills + transition variants (riser-hit, drop, half-time).
2. Options: include kick (off when user has own kick), include transitions, intensity 1–5, swing.
3. Sampler: `juce::Synthesiser` with round-robin + velocity layers; you supply royalty-free samples.
4. Export: full stereo WAV, or one WAV + MIDI per element.

## Acceptance
- Kick positions align to detected downbeats within 5 ms.
