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
| Tests | `build/tests/jaudios_tests`, `build/tests/jaudios_golden_tests`, `build/tests/jaudios_ml_golden_tests`, `build/tests/jaudios_stems_tests`, `build/tests/jaudios_transcription_tests`, `build/tests/jaudios_chords_tests` (chords + bass) |

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

## 3b. Stem model (HT-Demucs, needed for stem separation)

Stem separation uses HT-Demucs (`htdemucs`, MIT) exported to ONNX on your Mac. The export
downloads the 80 MB upstream checkpoint once (from `dl.fbaipublicfiles.com`, sha256-checked) and
writes a 174 MB `build/models/htdemucs.onnx` plus `htdemucs.onnx.provenance.json`. Nothing that
big is bundled with the app.

Add demucs to the environment from section 3. `--no-deps` matters: demucs' own requirements
(torchaudio and others) would replace the pinned torch, and the export doesn't need them:

```bash
~/.venvs/jaudios-ml/bin/pip install pyyaml==6.0.2 && ~/.venvs/jaudios-ml/bin/pip install --no-deps demucs==4.1.0 einops==0.8.1 julius==0.2.7
```

Export, check parity against upstream PyTorch (max |Δ| < 1e-3), and record provenance (about a minute):

```bash
python3 tools/models.py --only demucs --python ~/.venvs/jaudios-ml/bin/python
```

Like the key model, your export won't be byte-identical to the manifest's sha256; the app accepts
it through its provenance record. Copy both files if you move them
(`~/Library/Application Support/JAudios/models/` works too).

Separate a file from the command line (stems are 32-bit float WAVs; `guide.wav` sums the guide
stems, `other+vocals` by default):

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli separate tests/fixtures/house_124_Amin.wav --out /tmp/house-stems
```

Separated stems are cached by the SHA-256 of the decoded audio in
`~/Library/Application Support/JAudios/cache/stems/`, so separating the same audio again is instant.
About 250 MB per 3-minute track; the least recently used entries are removed above 4 GB. Use
`--no-cache` to bypass the cache, or `JAUDIOS_STEM_CACHE_DIR` to move it.

Separation runs on ONNX Runtime's CPU provider (default). The CoreML provider stays off until its
golden test passes on Apple Silicon (`JAUDIOS_ONNX_COREML=1` to try it). The CI's 3-core M1 runner
prints its 3-minute timing in the "Stem separation of a 3-minute track" step.


## 3c. Audio-to-MIDI model (Basic Pitch)

Basic Pitch (Spotify, Apache-2.0) ships its own ONNX model inside its PyPI package. The app uses
that 230 KB file unchanged. Fetching it needs only the system Python (no ML environment): it
downloads the pinned basic-pitch 0.4.0 wheel and checks the sha256 of both the wheel and the model.

```bash
python3 tools/models.py --only basic_pitch
```

Transcribe the mix, or a separated stem (separation needs the section 3b model and uses the stem
cache). The result is printed as JSON (notes with a confidence each) and written as a MIDI file:

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli transcribe tests/fixtures/melody_mono_Emin.wav --out /tmp/melody.mid
```

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli transcribe tests/fixtures/house_124_Amin.wav --stem bass
```

Options: `--stem mix|drums|bass|other|vocals|guide` (guide = other + vocals), `--mono` / `--poly`
(the bass stem defaults to monophonic), `--quantize 4|8|16|32|off` (default 16, on the detected
beat grid, swing kept), and the note thresholds `--onset 0.5 --frame 0.5 --min-ms 58`.

The golden tests that compare against upstream's Python code need its reference packages in the
ML environment. `--no-deps` keeps TensorFlow out; Basic Pitch runs its ONNX model on onnxruntime:

```bash
~/.venvs/jaudios-ml/bin/pip install --no-deps basic-pitch==0.4.0 mir_eval==0.8.2 pretty_midi==0.2.11.post0 mido==1.3.3 resampy==0.4.2 setuptools==75.8.0 decorator==5.3.1 six==1.17.0 importlib_resources==7.1.0
```

## 3d. Chord suggestions

The chord engine is rule-based (no model). It needs the audio-to-MIDI model from section 3c for the
melody and bass evidence, and uses the key model if it is there:

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli chords tests/fixtures/pop_100_Cmaj.wav --out /tmp/pop.chords.mid
```

It prints the 8 best progressions (chords, Roman numerals, score parts) and six alternatives for
each of the first four chords, and writes the best one as MIDI. Options: `--style
pop|house|techno|trance|lofi|rnb|cinematic`, `--rhythm 1|2|half` (chords per bar), `--sevenths`,
`--stems` (melody from the guide stems and bass from the bass stem; needs section 3b),
`--voicing close|open|drop2`, `--humanize 8`, `--seed N`, `--top N`.

## 3e. Basslines

Also rule-based, on top of the chord suggestions (section 3d):

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli bass tests/fixtures/house_124_Amin.wav --style house --out /tmp/house.bass.mid
```

It writes the bassline for the best progression as MIDI and prints every note with its role (root,
fifth, approach, …) and whether it meets a kick (a sidechain hint for later effects). Patterns live in
`engine/generators/patterns/*.json` (`--list-patterns` shows them, `--pattern NAME` picks one); new
ones must pass `python3 tools/validate_patterns.py`. Options: `--style`, `--grid 8|16|32`,
`--swing 0..0.75`, `--stems`. Kicks are detected on the mix's low band unless `--stems` is given.
With `--stems` they come from the drums stem, which is more accurate when the bass plays between
the kicks; the vocal stem's low notes are then kept clear too.

## 3f. Drums

A groove on the source's detected downbeats, rendered with JAudios' built-in kit. The kit is
synthesised by our own code, so no sample files are needed or shipped:

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli drums tests/fixtures/house_124_Amin.wav --style house --split --out /tmp/house-drums
```

This writes `drums.wav` + `drums.mid` (General MIDI, channel 10) and, with `--split`, one WAV + MIDI
per element (kick, snare, clap, hat, openhat, perc, ride). Options: `--pattern NAME`
(`--list-patterns`), `--intensity 1..5` (1 = kick + snare/clap, 2 adds hats, 3 open hats and ghost
notes, 4 perc, 5 ride), `--no-kick` (when you have your own kick), `--transition
fill|riser_hit|drop|half_time|none` on the last bar of every `--phrase` bars (default 8), `--swing`.
To use your own samples, put files named `<element>[_v<layer>][_rr<n>].wav` (e.g. `kick.wav`,
`snare_v2_rr3.wav`) in a folder and pass `--kit FOLDER`. Elements without files keep the built-in
sound. Groove files live in `engine/generators/drums/*.json` and must pass
`python3 tools/validate_patterns.py`.

## 3g. Built-in instruments (audition)

The chord, bass and drum channels play through built-in instruments in the engine graph, on the
same transport as the source. The melodic instruments are SFZ patches written for JAUDIOS and played
by sfizz (BSD-2, fetched and built with the rest of the project). They use sfizz's oscillators, so no
sample files are involved. The drums use the built-in kit from section 3f. To hear what the app
plays, render it offline:

```bash
build/tools/cli/app-cli_artefacts/Release/app-cli audition tests/fixtures/house_124_Amin.wav --style house --out /tmp/house.audition.wav
```

Options: `--chords PRESET`, `--bass PRESET` (`--list-presets`: piano and keys, pad, pluck, sub bass
and saw bass), `--no-drums`, `--stems`, `--bypass` (the source alone, as the app's bypass switch
plays it).

## 3h. Your VST3 instruments

The chord and bass channels can play VST3 instruments instead of the built-in patches. Plugins are
scanned **out of process**: `jaudios-plugin-scanner` (built next to the app and the CLI) opens one
plugin file at a time, so a plugin that crashes or hangs while being scanned only takes the scanner
down. It's recorded as failed and the scan carries on (30 s timeout per file). Found plugins go into
a known-plugin list at `~/Library/Application Support/JAudios/plugins.xml`
(`JAUDIOS_PLUGIN_LIST` or `--plugin-list FILE` overrides it).

```bash
CLI=build/tools/cli/app-cli_artefacts/Release/app-cli
"$CLI" plugins scan                 # /Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/VST3
"$CLI" plugins scan --path ~/MyPlugins/VST3
"$CLI" plugins list
```

Audition with a scanned plugin (by name), set its parameters (normalised 0..1, by name or ID), and
keep the setup in a project file:

```bash
"$CLI" audition tests/fixtures/house_124_Amin.wav --chords-plugin "Vital" --chords-param "Level=0.5" --save-project /tmp/song.json
"$CLI" audition tests/fixtures/house_124_Amin.wav --project /tmp/song.json    # same instruments, plugin state included
```

The project file holds each channel's instrument: a built-in preset, an SFZ file, the drum kit,
or a plugin, with its description and its own state blob (`getStateInformation`, base64). Flags
given on the command line override the project's slot for that channel.

The build also makes a small test instrument, `JAudios Test Synth`, under
`build/tests/plugins/TestSynth/` for the tests and CI. It's never installed or shipped. To try
hosting without third-party plugins: `"$CLI" plugins scan --path build/tests/plugins/TestSynth/JAudiosTestSynth_artefacts/Release/VST3`.

## 3i. Insert FX

Every channel (source, the four stems, chords, bass, drums) has its own insert chain, processed
in this order: filter → saturation → OTT-style 3-band compressor → sidechain duck → ping-pong
delay → reverb. Each stage is off until you switch it on. The ducker is keyed by the kicks of the
generated drums ("Kick events") or by the drums stem. The delay is synced to the detected tempo.
Every parameter change is smoothed: the tests sweep each one and check the spectrum for zipper
noise.

```bash
CLI=build/tools/cli/app-cli_artefacts/Release/app-cli
"$CLI" audition --list-fx      # channels, parameters, ranges, choices
"$CLI" audition tests/fixtures/house_124_Amin.wav --style house \
    --fx bass.duck_on=on --fx bass.duck_depth=0.9 \
    --fx chords.reverb_on=on --fx chords.delay_on=on --fx "chords.delay_division=1/8 dotted" \
    --fx drums.ott_on=on --out /tmp/house.fx.wav
```

Values are plain units (Hz, dB, ms), `on` / `off` for switches, and a choice by name.

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
| `error: htdemucs.onnx not found` | Export the stem model (section 3b) or set `JAUDIOS_MODELS_DIR`. |
| `error: basic_pitch.onnx not found` or "audio-to-MIDI model sha256 mismatch" | `python3 tools/models.py --only basic_pitch` (section 3c). |
| `ModuleNotFoundError: No module named 'demucs'` (or `einops` / `julius` / `yaml`) | Install the section 3b packages into the ML environment. |
| "stem model sha256 … not a verified export" | `htdemucs.onnx.provenance.json` is missing or doesn't match the file; re-run the section 3b export. |
| "key model sha256 mismatch … not a verified local export" | The model has no valid `openkeyscan3.onnx.provenance.json` next to it (copied without it, modified, or left behind by a failed export). Re-run the export in section 3, or copy both files. |
| "WARNING: onnxsim crashed (SIGSEGV)" during the export | Expected on Apple Silicon; the export continues with the unsimplified graph (see section 3). |
| The export dies with `SIGSEGV` in another stage | Run `python ml/tests/export_probe.py <raw.onnx> <model.onnx>` with the ML environment's Python to see which library crashes, and report it. |
| CMake picks `osx-x64` / the app runs under Rosetta | You are using x86_64 tools; see the `uname -m` check in section 1. |
| `cmake: CMake 3.25 or higher is required` | `brew upgrade cmake`. |
