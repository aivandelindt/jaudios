---
name: key-detection-openkeyscan
description: Musical key detection using the OpenKeyScan CNN (MIT). Phase A = sidecar process, Phase B = native ONNX in-process. Chroma/Krumhansl fallback for short clips. Use for anything touching key, Camelot, or Open Key output.
---

## Source of truth
- Repo: https://github.com/rekordcloud/openkeyscan-analyzer (MIT, © Alexander Sommer; extends
  a1ex90/MusicalKeyCNN, MIT). Pin a release tag/commit in models/manifest.json.
- Read before coding: README.md, CLAUDE.md, INTERFACING.md, openkeyscan_analyzer_server.py,
  openkeyscan_analyzer.py, preprocess_data.py, model.py.
- Model: checkpoints/custom/openkeyscan3.pt. Input: CQT spectrogram, 1 channel, 104 frequency bins,
  variable time frames. Output: 24 logits → class index 0–23 → Camelot (1A–12A minor, 1B–12B major),
  Open Key (1m–12m / 1d–12d), traditional name.
- Verified preprocessing (from repo docs; CONFIRM in preprocess_data.py and record in
  engine/analysis/openkeyscan/PREPROCESSING.md):
  sr 44100 Hz · hop 8820 samples (200 ms) · librosa CQT · 105 bins computed, 24 bins/octave,
  fmin 65 Hz (C2) · model consumes 104 bins → find out which bin is dropped and what magnitude/log
  scaling + normalisation is applied. Do not guess; copy the exact code path.

## Public interface (both phases implement this)
```cpp
struct KeyCandidate { Key key; int camelotNumber; char camelotLetter; float score; };
struct KeyResult {
  std::vector<KeyCandidate> top3;   // sorted desc by score
  float margin;                     // top1.score - top2.score (softmax)
  KeyBackend backend;               // Sidecar | Onnx | Chroma
};
class IKeyDetector {
 public:
  virtual ~IKeyDetector() = default;
  virtual KeyResult analyzeFile(const juce::File&) = 0;
  virtual KeyResult analyzeBuffer(const juce::AudioBuffer<float>&, double sr) = 0;
};
```
- Factory `makeKeyDetector(AppConfig)` reads `KEY_BACKEND = sidecar | onnx | chroma`.
- Runs on the Analysis thread only. Never on the audio thread, never on the message thread.

## Phase A — sidecar (milestone M1)
1. `make models` downloads the pinned platform zip (manifest id `openkeyscan_sidecar`), verifies
   sha256, unpacks to `build/sidecar/openkeyscan-analyzer/`. CMake copies it into the bundle at
   `Contents/Resources/sidecar/` (mac) / `resources/sidecar/` (win).
2. `KeyDetectorSidecar` (engine/analysis/openkeyscan/KeyDetectorSidecar.{h,cpp}):
   - Spawn with `juce::ChildProcess`: `openkeyscan-analyzer --device mps` (mac arm64),
     `--device cpu` (win, until DirectML is validated), `-w 1`. Keep alive for the app session.
   - Protocol is NDJSON over stdin/stdout (verified): request `{"id":"<uuid>","path":"/abs/file"}`,
     one JSON object per line back containing at least camelot, openkey, key (traditional) and class
     index. Mirror the `KeyDetectionService` wrapper in INTERFACING.md for lifecycle/ready handling.
     Log the exact response schema in PREPROCESSING.md on first run.
   - If the server exposes per-class scores/probabilities, use them for top3 + margin. If it returns
     only the argmax, set top3 = [argmax] with score 1.0, margin = 1.0, and mark `scoresAvailable=false`
     so the UI hides alternatives (Phase B fixes this properly).
   - `analyzeBuffer`: write temp WAV (44.1 kHz, 16-bit PCM, mono downmix) to the app cache dir,
     send its path, delete after response.
   - Timeouts: 30 s per request, 20 s for startup "ready". On crash/timeout: kill, restart once,
     then fall back to `KeyDetectorChroma` and surface a non-blocking warning.
   - Read stderr in a background thread into the app log at DEBUG level.
3. Signing: see packaging-and-release skill (every .so/.dylib in the sidecar must be re-signed).

## Phase B — native ONNX (milestone M2)
1. `ml/export_openkeyscan.py`: load openkeyscan3.pt with the repo's model.py → `torch.onnx.export`
   with dynamic time axis, opset 17 → onnxsim → `models/openkeyscan3.onnx`; print sha256 and update
   manifest. Assert: PyTorch vs ORT logits max abs diff < 1e-4 on 5 fixture spectrograms.
2. Complete engine/analysis/openkeyscan/PREPROCESSING.md from the actual code before writing C++.
3. `engine/dsp/CQT.{h,cpp}`: own implementation (or port of librosa pseudo/hybrid CQT, BSD-compatible
   — check the specific function's license lineage; no GPL). Golden test: C++ CQT vs
   `ml/tests/cqt_reference.py` (librosa, same params) on 5 fixtures, max abs diff < 1e-3 after the
   model's own scaling.
4. `KeyDetectorOnnx`: ORT session, CoreML EP → CPU fallback (DirectML on win). Works on float
   buffers directly, no temp files. Softmax over 24 logits → top3 + margin.
5. Keep Phase A code until Phase B passes acceptance; then remove the sidecar from the bundle and
   the manifest `openkeyscan_sidecar` entry (keep `openkeyscan3_pt` for re-export).

## Fallback / ensemble (`KeyDetectorChroma`, both phases)
- Chroma (12-bin, from the same CQT in Phase B) + Krumhansl-Schmuckler profiles. Cheap, no model.
- Input < 30 s (loops, single stems): run OpenKeyScan AND chroma. If they disagree, prefer OpenKeyScan
  unless `margin < 0.15`, then put the chroma result as top3[1] and flag `lowConfidence`.
- Always expose top3 to the UI (Camelot + traditional); user override persists per project.

## Acceptance
- ml/tests: GiantSteps Key (evaluation set, not the MTG training set) subset — MIREX weighted
  score ≥ 0.80, raw accuracy ≥ 0.75, for each backend that is enabled.
- Phase B equals Phase A argmax on ≥ 98% of the subset (same weights; only CQT drift can differ).
- Cold start: Phase A < 5 s to "ready"; Phase B < 300 ms session creation.
- Per 3-min track: < 2 s on Apple M-series, either phase.
- No allocation or blocking on the audio thread (verified by the realtime-audio skill's checker).

## Don'ts
- Don't retrain or fine-tune in this repo. If accuracy is insufficient, open an issue upstream.
- Don't ship the GiantSteps audio or annotations.
- Don't use "OpenKeyScan" or "Lexicon" in UI strings except the Credits line.
```
