# Changelog

## Unreleased

### key-detection-openkeyscan Phase B (native ONNX)
- `ml/export_openkeyscan.py`: upstream `eval.load_model()` (pinned checkout via `ml/openkeyscan_source.py`) →
  `torch.onnx.export` (opset 17, dynamic time axis, input `cqt` (1,1,105,T) → `logits` (1,24)) → onnxsim.
  Parity PyTorch vs ORT: max |Δlogits| 1.4e-6 (threshold 1e-4); export is deterministic, sha256 pinned in
  `models/manifest.json`. `make models` verifies / exports `build/models/openkeyscan3.onnx`.
- `ml/tests/cqt_reference.py`: upstream `preprocess_audio()` → reference CQT + PyTorch logits (.npy).
- `engine/dsp/CQT`: port of librosa 0.11.0 `cqt` for the model's configuration (two 2:1 decimations at hop 8820,
  sparse FFT basis, √length scaling). Matches librosa within 9.1e-6 (threshold 1e-3) on all fixtures.
- `KeyDetectorOnnx`: ORT session (CoreML EP → CPU on macOS), sha256-verified model, softmax → real top3 + margin
  (`scoresAvailable = true`), < 30 s chroma ensemble, chroma fallback with a warning. ORT session 93 ms; whole
  detector ~250 ms; key for a 62 s file ~470 ms (Linux x86, 4 vCPU).
- Full pipeline (our CQT + ORT) vs PyTorch on librosa input: identical argmax on all fixtures, max |Δlogits| 7.6e-6
  → Phase B == Phase A argmax.
- `KEY_BACKEND` default is now `onnx` (falls back to chroma with a "run `make models`" warning when the model is
  missing); `auto` prefers onnx → sidecar → chroma silently; `chroma` / `sidecar` explicit. Models looked up in
  `JAUDIOS_MODELS_DIR`, `~/Library/Application Support/JAudios/models`, bundle `Resources/models`, `build/models`.
- ONNX Runtime 1.19.2 from the official NuGet package (cmake/OnnxRuntime.cmake), copied into app bundles.
- Resampler: integer-ratio fast path (same kernel, bit-identical output); 2:1 decimation 5× faster.
- Own SHA-256 (`engine/util/Sha256`) for model verification (juce::SHA256 took ~280 ms on the 18.8 MB model).
- Model-backend golden cases are `[!mayfail]` while fixtures are synthetic (failures still printed).

### key-detection-openkeyscan Phase A (sidecar)
- `engine/analysis/openkeyscan`: `OpenKeyScanProtocol` (NDJSON parsing, class index ↔ key exactly as upstream
  `camelot_output()`, request builder, device arguments), `SidecarProcess` (POSIX fork/exec with stdin/stdout/stderr
  pipes: `juce::ChildProcess` cannot write to stdin; Windows not implemented yet → chroma), `KeyDetectorSidecar`
  (lazy start on the Analysis thread, 20 s ready / 30 s request timeouts, kill + restart once then chroma fallback
  with a warning, temp 44.1 kHz/16-bit mono WAV for `analyzeBuffer`, < 30 s ensemble with chroma, stderr → debug log).
- Server returns argmax only: sidecar results have `scoresAvailable = false`, margin 1.0; GUI hides alternatives.
- `KEY_BACKEND` default stays `chroma`; `auto` (sidecar when found, else chroma) and `sidecar` are opt-in;
  `OPENKEYSCAN_SIDECAR[_ARGS]` override the executable.
- `models/manifest.json` (pinned commit + checkpoint sha256), `make models` (`tools/models.py`),
  `ml/sidecar/build_openkeyscan_sidecar.sh` (PyInstaller build from the pinned source), CMake copies the sidecar
  into `Contents/Resources/sidecar/` (macOS) or `resources/sidecar/`.
- `PREPROCESSING.md` completed from the code: the model takes **105** bins (`[:, 0:-2]` drops two *time frames*),
  `fmin` is exactly 65.0 Hz, and PyAV (Win/Linux compressed formats) peak-normalises while librosa does not.
- Fake NDJSON server (`tests/fake_openkeyscan/fake_server.py`) for crash / hang / never-ready / error / noisy tests;
  golden tests gain a `[golden][sidecar]` case that runs when a sidecar is available.
- Fixed: test signal helper (and fixture renderer) read their scale tables through a lambda from function-local
  statics named `major`, which produced garbage pitches under GCC -O0 + ASan (Debug-only flaky key test).

### key-and-tempo-analysis (key-detection-openkeyscan: chroma backend; tempo-and-beatgrid)
- `engine/theory/Key`: key value type, Camelot / Open Key / traditional names + parsing (pure C++).
- `engine/analysis/key`: `IKeyDetector`, `KeyResult {top3, margin, backend, scoresAvailable, lowConfidence}`,
  `makeKeyDetector(AppConfig)` reading `KEY_BACKEND` (sidecar / onnx fall back to chroma with a warning until the
  OpenKeyScan dependency is approved).
- `KeyDetectorChroma`: STFT 16384/4096 → median-filter HPSS → spectral peaks → tuning estimate → HPCP
  (cos² semitone weighting + harmonic weighting h ≤ 8, 0.6^(h-1)) → correlation with mean of Krumhansl-Kessler and
  Temperley profiles → softmax (T = 0.05). Relative major/minor decided in two stages: pitch collection by profile
  fit, then mode by profile difference + opening-tonic evidence (weight 1.0; calibrate on GiantSteps).
- `engine/analysis/tempo/BeatTracker`: 40-band log-mel spectral flux at 100 fps → autocorrelation comb (60–200 BPM,
  4 harmonics, log-Gaussian prior at 120 BPM) → Ellis DP beat tracking → on/off-beat check on low-band (<150 Hz)
  attacks → per-beat attack refinement (1 ms) → robust constant-tempo grid fit (extended to attacks at both ends) →
  4/4 downbeat phase from harmonic change + attack + low-band accents → per-bar chroma.
- `engine/analysis/Analysis`: one call for key + rhythm (chroma computed once), stable JSON for `app-cli analyze`.
- GUI: import now also analyses on the Analysis thread; waveform shows beats + downbeats; key/tempo in status.
- Tests: theory, key (24 keys, detuning, drums, resampling, relative loops, factory), tempo (7 tempi, beat
  alignment, downbeats from accents and from harmony, shift stability, grid fit, silence, per-bar chroma), and
  `jaudios_golden_tests` driven by `ml/tests/goldens.json`.

### audio-capture (file import only; live capture is a later milestone)
- `engine/analysis/AudioImport`: WAV/AIFF/FLAC/Ogg (+ MP3/M4A/CAF via CoreAudio on macOS) → float buffer at
  44.1 kHz; `CaptureResult` keeps original sample rate, channel count, length, format, start time.
- `engine/analysis/Resampler`: own zero-phase Kaiser-windowed-sinc resampler (anti-aliased downsampling,
  bit-exact identity at 44.1 kHz, deterministic).
- GUI: drag-and-drop + Open dialog; import + waveform overview run on the Analysis thread, result handed back via
  `AnalysisService`; click-to-seek waveform.
- CLI: `app-cli import <file>`; `render` now goes through the importer.

### juce-standalone-scaffold
- CMake ≥ 3.25 project, C++20, JUCE 8.0.15 and Catch2 v3.9.1 via FetchContent (pinned tags).
- JUCE modules compiled once into `jaudios_juce`; engine static libs `engine_{analysis,theory,generators,synth,fx,export}`
  (empty ones are INTERFACE placeholders until their module lands).
- `app_core`: `AudioEngine` (owns `AudioDeviceManager` + `AudioProcessorGraph`: source player → channel strip
  (gain/mute/solo, smoothed) → master → out), lock-free `SpscQueue` (`juce::AbstractFifo`), `AnalysisService`
  (1-thread `juce::ThreadPool` + FIFO + `AsyncUpdater`).
- Fixed-size graph blocks + warm-up render in `prepare()`: JUCE's render sequence otherwise reallocates on the
  audio thread on the first callback and whenever the device block size changes.
- `tools/rt_check.sh` + realtime trap (interposed malloc/free/new/delete/pthread_mutex_lock counted inside
  `rt::ScopedRealtimeSection`). Fails on any audio-thread allocation; reports JUCE graph node locks.
- `app-cli info | render <file>`; `JAudios` GUI shell (device settings, transport).
- `fixture-render` + `fixtures` target: deterministic synthetic fixtures with ground truth.
- Debug builds use `-fsanitize=address,undefined`.
