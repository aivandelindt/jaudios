# Building JAudios on macOS (Apple Silicon)

JAudios targets Apple Silicon Macs first. Nothing in the build is chip-specific: these steps are the
same on an M1 through M5. Intel Macs and universal (arm64 + x86_64) builds are not supported yet.

> Verification status: these steps are exercised on every PR by the
> [macOS (Apple Silicon) workflow](../.github/workflows/macos.yml) on GitHub's `macos-15` arm64
> runners. That runner is a VM, so it does not have the Neural Engine; check CoreML performance on
> a real Mac.

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
download yet, so export it once from the pinned upstream checkpoint.

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

Export, verify parity (PyTorch vs ONNX Runtime) and write `build/models/openkeyscan3.onnx`:

```bash
python3 tools/models.py --only onnx --python ~/.venvs/jaudios-ml/bin/python
```

The app finds the model in, first match wins:

1. `$JAUDIOS_MODELS_DIR`
2. `~/Library/Application Support/JAudios/models/`
3. `JAudios.app/Contents/Resources/models/`
4. `build/models/` (development builds)

To use it outside the build tree:

```bash
mkdir -p ~/Library/Application\ Support/JAudios/models && cp build/models/openkeyscan3.onnx ~/Library/Application\ Support/JAudios/models/
```

On Apple Silicon the model runs through ONNX Runtime's CoreML execution provider, falling back to
the CPU if CoreML is unavailable. Run with `JAUDIOS_LOG_LEVEL=debug` to see which provider was
used.

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
| "key model sha256 mismatch" | The model file is not the one pinned in `models/manifest.json`. Re-run section 3; report it if a fresh export on your Mac still mismatches. |
| CMake picks `osx-x64` / the app runs under Rosetta | You are using x86_64 tools; see the `uname -m` check in section 1. |
| `cmake: CMake 3.25 or higher is required` | `brew upgrade cmake`. |
