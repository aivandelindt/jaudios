# Changelog

## Unreleased

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
