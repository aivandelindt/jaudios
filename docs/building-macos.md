# Building JAudios on macOS (Apple Silicon)

JAudios targets Apple Silicon Macs first. Nothing in the build is chip-specific: these steps are the
same on an M1 through M5. Intel Macs and universal (arm64 + x86_64) builds are not supported yet.

> Verification status: these steps are exercised on every PR by the
> [macOS (Apple Silicon) workflow](../.github/workflows/macos.yml) on GitHub's `macos-15` arm64
> runners (macOS 15, Xcode 16.4).

## 1. Prerequisites

| Tool | Version | Install |
|---|---|---|
| macOS | 13 or newer recommended (the ONNX Runtime library needs ≥ 11.0) | |
| Xcode | current release for your macOS (Apple Clang with C++20) | App Store, then `sudo xcode-select -s /Applications/Xcode.app` |
| CMake | ≥ 3.25 | `brew install cmake` |
| Ninja | any recent | `brew install ninja` |
| Python | 3.12 (only for the key model and the ML tests) | `brew install python@3.12` |
| git | any recent | ships with Xcode |

Use native arm64 tools. A Homebrew in `/usr/local` running under Rosetta makes CMake report
`x86_64` and you get an Intel build with the Intel ONNX Runtime. Check with:

```bash
uname -m && file "$(command -v cmake)"
```

Both should say `arm64`.

## 2. Build

From the repository root:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

The first configure downloads JUCE 8.0.15, Catch2 v3.9.1 and ONNX Runtime 1.19.2 (the official
Microsoft NuGet package, verified by sha256). This needs network access once.

Outputs:

| What | Where |
|---|---|
| App | `build/app/JAudios_artefacts/Release/JAudios.app` |
| CLI harness | `build/tools/cli/app-cli_artefacts/Release/app-cli` |
| Tests | `build/tests/jaudios_tests`, `build/tests/jaudios_golden_tests`, `build/tests/jaudios_ml_golden_tests` |

The ONNX Runtime dylib is copied into `JAudios.app/Contents/Frameworks/` automatically.

## 3. Key model (needed for the default `KEY_BACKEND=onnx`)

Without the model, key detection falls back to the built-in chroma detector and prints
"Key model openkeyscan3.onnx not found (run `make models`); using chroma". There is no hosted
download yet, so export it once on your Mac from the pinned upstream checkpoint.

Create a Python 3.12 environment with the pins from `models/manifest.json`:

```bash
python3.12 -m venv ~/.venvs/jaudios-ml
```

```bash
~/.venvs/jaudios-ml/bin/pip install torch==2.2.2 numpy==1.26.4 librosa==0.11.0 soundfile==0.13.1 av==16.1.0 scipy==1.17.1 numba==0.64.0 llvmlite==0.46.0 scikit-learn==1.8.0 soxr==1.0.0 audioread==3.1.0 tqdm onnx==1.16.2 onnxsim==0.6.5 onnxruntime==1.19.2
```

The export's parity check needs the test fixtures:

```bash
cmake --build build --target fixtures
```

Export, verify parity (PyTorch vs ONNX Runtime) and write `build/models/openkeyscan3.onnx` plus
`openkeyscan3.onnx.provenance.json`:

```bash
python3 tools/models.py --only onnx --python ~/.venvs/jaudios-ml/bin/python
```

Your export will almost certainly not be byte-identical to the sha256 in `models/manifest.json`:
the export folds constants with CPU-specific ONNX Runtime code, so even two Linux x86-64 machines
produce different bytes. That is expected. The app accepts such a model when the provenance
record next to it matches the file, comes from the pinned upstream commit and checkpoint, and
records a passing parity check (max |Δlogits| < 1e-4). Keep the two files together when you copy
the model anywhere. The app logs "verified local export from darwin-arm64" when it loads one.

onnxsim 0.6.5 (the graph simplifier) segfaults on Apple Silicon. The export detects that,
prints a warning, and uses the unsimplified graph instead: for this model it has the same ops and
bit-identical weights, and the parity check still runs. The provenance record then says
`"simplified": false`.

**Alternative:** every CI run uploads a verified model (with its provenance record) as the
`openkeyscan3-onnx` artifact. With the GitHub CLI:

```bash
gh run download --repo aivandelindt/jaudios --name openkeyscan3-onnx --dir build/models "$(gh run list --repo aivandelindt/jaudios --workflow macos.yml --branch main --status success --limit 1 --json databaseId --jq '.[0].databaseId')"
```

The app finds the model in, first match wins:

1. `$JAUDIOS_MODELS_DIR`
2. `~/Library/Application Support/JAudios/models/`
3. `JAudios.app/Contents/Resources/models/`
4. `build/models/` (development builds)

To use it outside the build tree:

```bash
mkdir -p ~/Library/Application\ Support/JAudios/models && cp build/models/openkeyscan3.onnx build/models/openkeyscan3.onnx.provenance.json ~/Library/Application\ Support/JAudios/models/
```

The model runs on ONNX Runtime's CPU execution provider (about 40 ms per track). The CoreML
provider is off: with ONNX Runtime 1.19.2 it runs only part of this model and returns wrong keys
(found by the macOS CI golden test). `JAUDIOS_ONNX_COREML=1` turns it on for investigation only.
Run with `JAUDIOS_LOG_LEVEL=debug` to see the provider and load timings.

## 4. Run

The app:

```bash
open build/app/JAudios_artefacts/Release/JAudios.app
```

Drop an audio file on the waveform, or use **Open…**. WAV, AIFF, FLAC, Ogg, MP3 and M4A are
supported; MP3/M4A decode through Core Audio. macOS asks for microphone permission the first time
the app opens an audio input.

The CLI on a fixture:

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli analyze tests/fixtures/house_124_Amin.wav
```

Choose the key backend with `KEY_BACKEND=onnx|chroma|sidecar|auto` (default `onnx`).

## 5. Test

```bash
ctest --test-dir build --output-on-failure
```

To also run the CQT and ONNX golden tests against the Python reference, configure with the ML
environment:

```bash
cmake -B build -DJAUDIOS_ML_PYTHON="$HOME/.venvs/jaudios-ml/bin/python"
```

Debug builds enable AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-debug && ctest --test-dir build-debug
```

Apple Clang has no LeakSanitizer on macOS, so leak checking only happens on Linux.

Realtime-safety check (builds an instrumented tree in `build-rt/`):

```bash
tools/rt_check.sh
```

On macOS it counts C++ `new`/`delete` on the audio thread. The `malloc` and mutex interposers are
Linux (glibc) only.

## 6. Optional: Phase A sidecar

The Python sidecar (`KEY_BACKEND=sidecar`) is being replaced by the ONNX model and is not needed
for the default setup. To build it on a Mac (Python 3.12, about 1 GB of downloads, about 780 MB
bundle):

```bash
ml/sidecar/build_openkeyscan_sidecar.sh build
```

Then unpack it to `build/sidecar/` and re-configure so it is copied into the app bundle:

```bash
python3 tools/models.py --only sidecar
```

Signing and notarizing the sidecar is covered by the packaging-and-release skill.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `Library not loaded: @rpath/libonnxruntime.1.19.2.dylib` | Re-configure and rebuild; the dylib is staged under its install name in `build/onnxruntime/lib/` and copied into the app bundle. |
| Key backend is `chroma` with "model not found" | Export the model (section 3) or set `JAUDIOS_MODELS_DIR`. |
| "key model sha256 mismatch … not a verified local export" | The model has no valid `openkeyscan3.onnx.provenance.json` next to it (copied without it, modified, or left behind by a failed export). Re-run the export in section 3, or copy both files. |
| "WARNING: onnxsim crashed (SIGSEGV)" during the export | Expected on Apple Silicon; the export continues with the unsimplified graph (see section 3). |
| The export dies with `SIGSEGV` in another stage | Run `python ml/tests/export_probe.py <raw.onnx> <model.onnx>` with the ML environment's Python to see which library crashes, and report it. |
| CMake picks `osx-x64` / the app runs under Rosetta | You are using x86_64 tools; see the `uname -m` check in section 1. |
| `cmake: CMake 3.25 or higher is required` | `brew upgrade cmake`. |
