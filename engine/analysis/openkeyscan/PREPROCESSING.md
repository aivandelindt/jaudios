# OpenKeyScan preprocessing — exact parameters

Pinned source: rekordcloud/openkeyscan-analyzer @ <tag/commit>  (fill from models/manifest.json)
Files read: preprocess_data.py, openkeyscan_analyzer.py, openkeyscan_analyzer_server.py, model.py

## Audio loading
- Decoder: <soundfile / audioread / PyAV — confirm which the server uses, and whether it resamples>
- Target sample rate: 44100 Hz (repo docs)
- Channels: <mono downmix method — mean? first channel?>
- Duration handling: <full track? trimmed? padded?>

## CQT (librosa)
- function: <librosa.cqt / hybrid_cqt / pseudo_cqt>
- hop_length: 8820 (200 ms) (repo docs)
- n_bins: 105 (repo docs) → model input 104: <which bin is dropped and where in code>
- bins_per_octave: 24 (repo docs)
- fmin: 65 Hz (C2) (repo docs) — confirm exact value (65.0 vs librosa.note_to_hz("C2") = 65.406)
- window / filter_scale / norm / sparsity / tuning: <fill>
- magnitude: <abs? power?>; scaling: <log1p? amplitude_to_db(ref=?)? none?>
- per-file normalisation: <fill>
- dtype at model input: float32, shape (1, 1, 104, T)

## Server protocol (openkeyscan_analyzer_server.py)
- Request line:  {"id": "<string>", "path": "<absolute path>"}
- Response line: <paste exact JSON keys observed; note whether per-class scores are included>
- Ready signal: <how does the server announce readiness — line on stdout? immediate?>
- Error shape: <fill>

## Class index → key mapping
| idx | traditional | camelot | openkey |
|-----|-------------|---------|---------|
| 0   | <fill>      |         |         |
| ... |             |         |         |
| 23  | <fill>      |         |         |

Golden: tests/fixtures/house_124_Amin.wav → A minor → 8A → 1m

---
Status (M1): **not started.** Phase A (sidecar) needs the OpenKeyScan analyzer bundle, which is not in
the AGENTS.md stack table; it is waiting for dependency approval. Until then `makeKeyDetector` maps
`KEY_BACKEND=sidecar|onnx` to the chroma backend and returns a warning.
