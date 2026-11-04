---
name: audio-to-midi
description: Convert vocal/bass/instrument stems to MIDI with Basic Pitch (ONNX).
---
## Steps
1. `/ml/export_basic_pitch.py` → `basic_pitch.onnx` (Apache-2.0). Input 22.05 kHz mono, 2 s windows.
2. C++ `AudioToMidi`: run model → onset/note/contour posteriors → note events (threshold 0.5, min length 58 ms, onset threshold 0.5; expose as advanced settings).
3. Quantize to beat grid from key-and-tempo-analysis (optional, default 1/16 with swing preservation).
4. Bass stem: force monophonic (keep highest-energy note), pitch-bend to MIDI CC/bend.
5. Output `juce::MidiMessageSequence` + per-note confidence for UI shading.

## Acceptance
- Monophonic vocal test set: note F1 ≥ 0.80 (onset tolerance 50 ms).
