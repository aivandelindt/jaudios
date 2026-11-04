# OpenKeyScan preprocessing — exact parameters

Pinned source: rekordcloud/openkeyscan-analyzer @ `6a2915f43b3bf2c825ddf508e2a703ffd4df8fb6`
(no tags upstream; see `models/manifest.json`). Checkpoint `checkpoints/openkeyscan3.pt`,
sha256 `5610691be2b3aa21764c5978ca0c2524fcdbf600f09dcbda6c2da937745db600`, 18 830 452 bytes.
Files read: README.md, INTERFACING.md, openkeyscan_analyzer_server.py, dataset.py, model.py,
openkeyscan_analyzer.spec, build-linux-x64.sh. Everything below is copied from the **server**
code path (`preprocess_audio()`), which is what Phase A runs and what Phase B must reproduce.

## Audio loading (server `preprocess_audio`)
- Two decoders, chosen by platform **and** extension:
  - `sys.platform in {'win32','linux'}` and suffix in `{.mp3,.mp4,.m4a,.aac}` → PyAV
    (`load_audio_pyav_optimized`): `av.AudioResampler(format='flt', layout='mono', rate=44100)`
    (FFmpeg's downmix + resampler), then **peak-normalised to 0.95** (`w / max|w| * 0.95`).
  - everything else (WAV/FLAC/OGG/AIFF/AU everywhere, and *all* formats on macOS) →
    `librosa.load(path, sr=44100, mono=True)` (librosa 0.11.0: mono = mean of channels,
    resampling `res_type='soxr_hq'`), **no normalisation**, cast to float32.
- ⚠ `log1p` is not scale-invariant, so the same MP3 gives a different spectrogram on macOS
  (librosa, raw level) than on Windows/Linux (PyAV, peak 0.95). Phase B must pick one; our Phase A
  `analyzeBuffer` always sends a WAV, which takes the librosa path on every platform.
- Duration: full file, no trimming or padding.
- Supported suffixes: `.mp3 .mp4 .wav .flac .ogg .m4a .aac .aiff .au` (others → error response).

## CQT (librosa 0.11.0)
```python
cqt  = librosa.cqt(waveform, sr=44100, hop_length=8820, n_bins=105, bins_per_octave=24, fmin=65)
spec = np.log1p(np.abs(cqt))
chunk = spec[:, 0:-2]                       # comment upstream says "Remove last frequency bin"
tensor = torch.tensor(chunk, dtype=float32).unsqueeze(0)   # (1, 105, T-2), then .unsqueeze(0) for batch
```
- function: `librosa.cqt` (all other arguments at librosa 0.11.0 defaults: `tuning=0.0`,
  `filter_scale=1`, `norm=1`, `sparsity=0.01`, `window='hann'`, `scale=True`, `pad_mode='constant'`,
  `res_type='soxr_hq'`).
- hop_length 8820 (200 ms), n_bins 105, bins_per_octave 24, **fmin = 65.0 Hz exactly** (not
  `note_to_hz('C2')` = 65.406; the grid is ~10.6 cents flat of equal temperament).
- magnitude: `abs`; scaling: `log1p`; **no per-file normalisation** of the spectrogram.
- ⚠ **Which bin is dropped: none.** `spec[:, 0:-2]` slices axis 1 of a `(105, T)` array, i.e. it
  drops the **last two time frames**, not a frequency bin. The model input is `(B, 1, 105, T-2)`;
  `model.py`'s own smoke test uses `torch.randn(2, 1, 105, 100)`. The skill's "model consumes 104
  bins" is therefore wrong for openkeyscan3 at this commit; Phase B must export with 105 bins.
- model: all-convolutional, `AdaptiveAvgPool2d((1,1))` → any T works. Output: 24 logits;
  the server takes `argmax` only (no softmax / scores are returned).

## Server protocol (openkeyscan_analyzer_server.py) — verified
- Command line: `openkeyscan-analyzer [-m MODEL] [--device cpu|cuda|mps] [-w WORKERS]`.
  Without `--device` it auto-selects CUDA > MPS > CPU. We pass `-w 1 --device mps` on Apple
  Silicon and `-w 1 --device cpu` elsewhere.
- Request line (stdin): `{"id": "<string>", "path": "<absolute path>"}`
  (also `{"type": "reset"}` → `{"type": "reset_complete"|"reset_idle", "generation": n}`; unused).
- Ready signal: one stdout line `{"type": "ready"}` after models are preloaded and a CQT warm-up
  ran; then `{"type": "heartbeat"}` every 30 s. Logs go to stderr.
- Success line:
  `{"id", "status": "success", "camelot": "8A", "openkey": "1m", "key": "A minor", "class_id": 7, "filename", "generation"}`
  `key` lists enharmonic names joined by `/` and sorted (e.g. `"Ab minor/G# minor"`).
  **No per-class scores or probabilities** → our `KeyResult`: top3 = [argmax], score 1.0,
  margin 1.0, `scoresAvailable = false`.
- Error line: `{"id", "status": "error", "error": "<message>", "filename", "generation"}`
  (e.g. `"File not found"`, `"Unsupported format. Supported: ..."`, exception text).
- Responses can arrive out of order (thread pool); match by `id`.

## Class index → key mapping
`camelot_output()`: number = `class % 12 + 1`, letter = `'A' if class < 12 else 'B'`;
names from `dataset.py` `CAMELOT_MAPPING`. Implemented in `OpenKeyScanProtocol.cpp`, tested for
all 24 classes; the client also rejects a reply whose `camelot` disagrees with `class_id`.

| idx | traditional | camelot | openkey |   | idx | traditional | camelot | openkey |
|-----|-------------|---------|---------|---|-----|-------------|---------|---------|
| 0   | G#/Ab minor | 1A      | 6m      |   | 12  | B major     | 1B      | 6d      |
| 1   | D#/Eb minor | 2A      | 7m      |   | 13  | F#/Gb major | 2B      | 7d      |
| 2   | A#/Bb minor | 3A      | 8m      |   | 14  | C#/Db major | 3B      | 8d      |
| 3   | F minor     | 4A      | 9m      |   | 15  | G#/Ab major | 4B      | 9d      |
| 4   | C minor     | 5A      | 10m     |   | 16  | D#/Eb major | 5B      | 10d     |
| 5   | G minor     | 6A      | 11m     |   | 17  | A#/Bb major | 6B      | 11d     |
| 6   | D minor     | 7A      | 12m     |   | 18  | F major     | 7B      | 12d     |
| 7   | A minor     | 8A      | 1m      |   | 19  | C major     | 8B      | 1d      |
| 8   | E minor     | 9A      | 2m      |   | 20  | G major     | 9B      | 2d      |
| 9   | B minor     | 10A     | 3m      |   | 21  | D major     | 10B     | 3d      |
| 10  | F#/Gb minor | 11A     | 4m      |   | 22  | A major     | 11B     | 4d      |
| 11  | C#/Db minor | 12A     | 5m      |   | 23  | E major     | 12B     | 5d      |

Golden: tests/fixtures/house_124_Amin.wav → A minor → 8A → 1m (class 7)

## Phase A end-to-end check (2026-11-04, Linux x86-64, 4 vCPU, server run from source, CPU)
Server = `openkeyscan_analyzer_server.py` @ pinned commit in a Python 3.12 venv with the manifest pins
(PyPI torch 2.2.2+cu121 running on CPU), driven by `KeyDetectorSidecar` via
`KEY_BACKEND=sidecar OPENKEYSCAN_SIDECAR=<venv>/bin/python OPENKEYSCAN_SIDECAR_ARGS=<server.py>`.
- "ready": 3.0–4.8 s (skill target < 5 s). The very first run after `pip install` took ~25 s
  (one-off bytecode/JIT cache warm-up) — a PyInstaller bundle should not show this, but check on a
  clean Mac.
- Per request: 0.12–0.48 s (librosa.load 0.13 s, CQT 0.18 s, inference 0.09 s on a 62 s file).
- Keys on the **synthetic** fixtures: house 8B (exp. 8A), techno 4A ✓, pop 8A (exp. 8B),
  loop 9A (exp. 9B), bass stem 8B (exp. 7A). Three are relative-key swaps: the synthetic loops are
  harmonically ambiguous by construction (e.g. Am-F-C-G = C major's vi-IV-I-V). These numbers say
  nothing about real-music accuracy; judge Phase A on real fixtures and the GiantSteps nightly.

## Phase B status (2026-11-04)
- Export: `ml/export_openkeyscan.py`, sha256 `2762d4a06f095a2d74372f1ab581f9cba154ac556f0fa89f09aa2d27fc6f53ff`,
  105 frequency bins (not 104, see above), dynamic time axis.
- C++ CQT (engine/dsp/CQT) vs this exact code path: max |Δ| 9.1e-6 after log1p (skill threshold 1e-3).
- Our CQT + ORT vs PyTorch on librosa input: same argmax on all 5 fixtures, max |Δlogits| 7.6e-6.
- With real scores, the model's runner-up is the expected key on every synthetic fixture it misses
  (house 8B/8A 0.54/0.35, pop 8A/8B 0.54/0.41, loop 9A/9B, bass 8B/7A).
- CoreML EP on Apple Silicon (macos-15 arm64 CI, ORT 1.19.2): accepts 14 of 23 nodes (10 partitions); the logits
  are off by 3-5 and the argmax changes on 2 of 5 fixtures (house 19 -> 7, bass 19 -> 6). CPU EP is therefore the
  default on macOS too; `[onnx][coreml][!mayfail]` keeps measuring it.
- Not yet run: GiantSteps subset (MIREX ≥ 0.80).
  Phase A stays until those pass (skill: "Keep Phase A code until Phase B passes acceptance").
- Export on macOS arm64: onnxsim 0.6.5 segfaults (even alone; `ml/tests/export_probe.py`, macos-15 CI). For this
  model onnxsim is a no-op: the raw export already has BatchNorm fused into Conv and the same 23 ops with
  bit-identical weights. The export falls back to the raw graph and records `"simplified": false`.
