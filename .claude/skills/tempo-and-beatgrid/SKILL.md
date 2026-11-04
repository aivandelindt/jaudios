---
name: tempo-and-beatgrid
description: Detect tempo and beat grid from audio using spectral flux, comb-filter BPM estimation, and dynamic-programming downbeat tracking.
---
## Steps
1. Onset envelope: compute spectral flux from the audio signal, then smooth and normalize to get a robust onset curve.
2. Tempo: autocorrelation / comb filter on the onset envelope for 60–200 BPM; choose the dominant tempo and its harmonic aliases.
3. Beat tracking: use dynamic programming / Ellis-style beat tracking to estimate beat positions aligned to the strongest tempo.
4. Downbeat estimate: infer bar boundaries from beat grid + harmonic changes + energy accents; output downbeats and bar anchors.
5. Per-bar chroma: compute per-bar chroma features for chord recognition and source analysis, enabling the chord engine to score detected harmony.
6. Manual override: if the user adjusts key or tempo, quantize generated parts to the updated grid.

## Acceptance
- Tempo within ±2% on the GiantSteps tempo subset, ≥ 90%.
- Beat positions remain stable across live captures and imported files of the same source.
- Downbeat alignment remains within 5–10 ms for fixed-bar source material used in the test fixtures.
