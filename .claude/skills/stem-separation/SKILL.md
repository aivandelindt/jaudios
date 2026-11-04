---
name: stem-separation
description: 4-stem (vocals/drums/bass/other) separation with HT-Demucs via ONNX Runtime, CoreML on Apple Silicon.
---
## Steps
1. `/ml/export_demucs.py`: load `htdemucs` (MIT), export to ONNX with dynamic time axis, opset 17. Verify SDR vs PyTorch on 3 reference tracks (tolerance 0.1 dB).
2. C++: `StemSeparator` wraps onnxruntime; execution providers in order: CoreML → CPU (mac), DirectML → CPU (win). Chunked inference (~7.8 s segments, 25% overlap, Hann crossfade).
3. Progress callback for UI; cancellable.
4. Output `Stems {vocals, drums, bass, other}` as float buffers, same length as input, sample-aligned.
5. Cache by SHA-256 of input in Application Support.
6. Expose per-stem solo/mute/gain to the audio graph; "guide stem" selector decides which stem feeds audio-to-midi and chord analysis (default: other+vocals).

## Acceptance
- 3-minute track separates in < 60 s on M-series.
- Sum of stems vs. original: null test < -40 dBFS RMS.
