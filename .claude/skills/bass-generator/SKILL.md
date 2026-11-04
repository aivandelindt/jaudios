---
name: bass-generator
description: Generate basslines locked to the chord progression and detected groove.
---
## Steps
1. Input: progression, BPM, beat grid, style, detected kick positions (from drums stem onsets).
2. Rhythm patterns per style (root-on-1, offbeat house, rolling 1/16 techno, syncopated funk, walking). Patterns stored as JSON in `/engine/generators/patterns/`.
3. Pitch rules: root on downbeat; passing tones from scale; octave jumps ≤ 20%; approach notes on bar end; avoid clashing with detected vocal low notes (< E3).
4. Accent editor: per-step velocity + gate, loop length 1/2/4 bars, grid 1/8–1/32, swing %.
5. Sidechain hint: mark notes that overlap kick onsets for the FX sidechain.

## Acceptance
- Every generated note is a chord tone or scale tone of the current chord (test).
- Pattern JSON validated against schema in CI.
