# Third-party components

| Component            | Version / commit            | License    | Copyright                               | Used for                    |
|----------------------|-----------------------------|------------|-----------------------------------------|-----------------------------|
| JUCE                 | 8.0.15 (FetchContent, pinned tag) | AGPL/Commercial → **commercial license required before release** (tracked in #TBD) | Raw Material Software | App framework |
| Catch2               | v3.9.1 (FetchContent, pinned tag) | BSL-1.0 | Catch2 authors | Unit tests only, never shipped |
| ONNX Runtime         | 1.19.2 — official prebuilt binaries from the `Microsoft.ML.OnnxRuntime` NuGet package (cmake/OnnxRuntime.cmake, sha256 fa133c8f…) | MIT | Microsoft Corporation | Phase B key model inference (CPU; CoreML EP on macOS). Linked dynamically; package ThirdPartyNotices.txt to be shipped with the app |
| Demucs (htdemucs)    | demucs 4.1.0, checkpoint `955717e8-8726e21a.th` (sha256 8726e21a…, models/manifest.json) | MIT | Meta Platforms, Inc. | Stem separation. Weights exported to ONNX on the user's machine (ml/export_demucs.py), never bundled (174 MB); STFT/ISTFT and segmenting re-implemented in C++ from demucs 4.1.0 |
| Basic Pitch          | basic-pitch 0.4.0 wheel, `nmp.onnx` (ICASSP 2022 model), sha256 2c3c1d14… (models/manifest.json) | Apache-2.0 | Spotify AB | Audio-to-MIDI. Upstream's own ONNX file, fetched by ml/fetch_basic_pitch.py and used byte-for-byte (230 KB); inference windowing and note creation re-implemented in C++ from basic-pitch 0.4.0. NOTICE to ship with the app |
| OpenKeyScan analyzer | commit 6a2915f4 (models/manifest.json; no upstream tags) | MIT | Alexander Sommer (rekordcloud) | Key detection (model + Phase A sidecar). Built from source, not vendored; clone lives in ml/third_party/ (gitignored) |
| MusicalKeyCNN        | upstream of OpenKeyScan     | MIT        | a1ex90                                  | Model architecture / training code lineage |
| GiantSteps Key datasets | —                        | CC BY-NC-SA 4.0 (annotations) | Knees et al. / MTG | **Tests only, never shipped** |

### ML export tooling (dev only, ml/; never shipped or linked)
| Component | Version | License | Used for |
|---|---|---|---|
| onnx (Python) | 1.16.2 | Apache-2.0 | required by `torch.onnx.export` (approved 2026-11-04) |
| onnxsim | 0.6.5 | Apache-2.0 | graph simplification after export (approved 2026-11-04). Segfaults on macOS arm64; the export then uses the raw graph (numerically identical for this model) |
| onnxruntime (Python) | 1.19.2 | MIT | PyTorch-vs-ORT parity check |
| PyTorch / librosa / NumPy | see sidecar pins | BSD-3 / ISC / BSD-3 | export + reference CQT (`ml/tests/cqt_reference.py`) |
| demucs (Python) | 4.1.0 | MIT | loads htdemucs for the ONNX export + parity reference (`ml/export_demucs.py`); the stack table's HT-Demucs |
| einops / julius / PyYAML | 0.8.1 / 0.2.7 / 6.0.2 | MIT / MIT / MIT | import-time dependencies of demucs 4.1.0 (installed with `--no-deps` alongside it) |
| soundfile | 0.13.1 | BSD-3 | reads fixtures for the Demucs parity check (already a sidecar pin) |
| basic-pitch (Python) | 0.4.0 (`--no-deps`, no TensorFlow) | Apache-2.0 | upstream reference for the AudioToMidi goldens (`ml/tests/basic_pitch_reference.py`; approved 2026-11-07) |
| mir_eval / pretty_midi / mido / resampy | 0.8.2 / 0.2.11.post0 / 1.3.3 / 0.4.2 | MIT / MIT / MIT / ISC | import-time dependencies of basic-pitch's inference + note creation (approved 2026-11-07) |
| setuptools | 75.8.0 | MIT | provides `pkg_resources`, which resampy 0.4.2 imports |
| decorator / six / importlib_resources | 5.3.1 / 1.17.0 / 7.1.0 | BSD-2 / MIT / Apache-2.0 | requirements of mir_eval and pretty_midi (pinned so the `--no-deps` install is complete) |

### Ported code
| Component | Version | License | Used for |
|---|---|---|---|
| librosa `cqt`/`vqt`, `filters.wavelet*`, `util.sparsify_rows` | 0.11.0 | ISC (© librosa development team) | algorithm ported to engine/dsp/CQT.cpp (no code copied verbatim); credit in About |

### Bundled inside the Phase A sidecar (M1 only; PyInstaller bundle, separate process, removed in M2)
Pins from models/manifest.json (same as upstream build-linux-x64.sh / Pipfile.lock). Runs as its own
executable; nothing below is linked into JAudios.

| Component | Version | License | Note |
|---|---|---|---|
| CPython | 3.12 | PSF-2.0 | interpreter embedded by PyInstaller |
| PyTorch | 2.2.2 | BSD-3-Clause | inference (Linux PyPI wheel also pulls NVIDIA CUDA runtime libs, NVIDIA EULA — the build script prefers the CPU-only wheel) |
| NumPy | 1.26.4 | BSD-3-Clause | |
| SciPy | 1.17.1 | BSD-3-Clause | |
| librosa | 0.11.0 | ISC | CQT, audio loading |
| soundfile / libsndfile | 0.13.1 | BSD-3-Clause / LGPL-2.1 (shared lib) | |
| soxr (python-soxr / libsoxr) | 1.0.0 | LGPL-2.1 (shared lib) | resampling |
| audioread | 3.1.0 | MIT | |
| PyAV / FFmpeg | 16.1.0 | BSD-3-Clause / LGPL-2.1+ (shared libs) | compressed formats on Windows/Linux |
| numba / llvmlite | 0.64.0 / 0.46.0 | BSD-2-Clause | librosa dependency |
| scikit-learn | 1.8.0 | BSD-3-Clause | librosa dependency |
| PyInstaller | 6.19.0 | GPL-2.0 **with bootloader exception** | build tool; the exception permits distributing the frozen app under any license |

**Review before release:** the PyAV wheel's bundled FFmpeg build must be LGPL (not a GPL
configuration); verify with `ffmpeg -L`/`av.library_versions` on the built bundle.

## Attribution text (shown in About → Credits)
Key detection powered by OpenKeyScan (© Alexander Sommer, MIT License), based on
MusicalKeyCNN (© a1ex90, MIT License). "OpenKeyScan" is used here as attribution only;
it is not used in product naming or marketing.
