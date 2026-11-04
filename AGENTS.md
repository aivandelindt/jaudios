# Project: JAudios — standalone harmonic idea generator

## Mission
Standalone macOS (Apple Silicon first) app that: captures or imports audio, separates stems, detects key, converts stems to MIDI, suggests chord progressions and basslines that fit the source, adds genre drums, plays via built-in sounds or VST3, and exports MIDI/WAV (drag-and-drop into any DAW).

## Non-negotiables
- C++20, JUCE 8, CMake ≥ 3.25. No Projucer files.
- Real-time thread: no allocation, no locks, no logging. All ML runs on a worker thread.
- Every DSP/theory module has a unit test before it is wired into the UI.
- Models: ONNX only, loaded from `~/Library/Application Support/<App>/models/`, never bundled > 200 MB.
- Licenses: MIT/Apache/BSD/LGPL-dynamic only. Reject GPL/AGPL deps. Record every dep in THIRD_PARTY.md.
- Never copy Mixed In Key assets, UI, strings, or presets.

## Repo layout
- /app: JUCE standalone (GUI + engine glue)
- /engine: pure C++ libs: analysis/, theory/, generators/, synth/, fx/, export/
- /ml: Python: model export scripts (Demucs, Basic Pitch) → ONNX, eval scripts
- /models: .onnx (git-lfs) + checksums
- /tests: Catch2 (C++) + pytest (ML goldens)
- /skills: SKILL.md files (this kit)

## Build & test
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python -m pytest ml/tests
```

## Working agreement
1. Read the relevant `.claude/skills/*/SKILL.md` before touching a module.
2. One module per PR; include tests and a CHANGELOG entry.
3. Ask before adding a dependency.
4. Definition of done: builds on arm64 macOS, tests green, no RT-thread violations (run `tools/rt_check.sh`), feature demoed via CLI harness before UI.


Stack decision (bake this into the agent's instructions)

| Layer | Choice | Why |
|---|---|---|
| App shell + audio engine + GUI | **JUCE 8 (C++20), CMake, standalone target** | Real-time audio, VST3 hosting, MIDI, drag-and-drop export, macOS/Windows from one codebase |
| Stem separation | **HT-Demucs (MIT)** exported to **ONNX → ONNX Runtime with CoreML EP** (Apple Silicon) | Offline, runs on your M5 Neural Engine/GPU |
| Audio → MIDI | **Spotify Basic Pitch (Apache-2.0)**, ONNX model | Polyphonic note + pitch-bend output, small model |
| Key detection | Own implementation: HPCP/chroma + Krumhansl/Temperley profiles | Avoids GPL (libKeyFinder) / AGPL (Essentia) |
| Chord & bass generation | Own rule-based engine (functional harmony + voice-leading) | Same approach Mixedinkey Wingman uses; fully testable |
| Built-in sounds | **sfizz** (SFZ, BSD-2) or **FluidSynth** (LGPL) + your own samples | No licensing trap |
| Effects | JUCE DSP: filter, delay, reverb, OTT-style multiband compressor, sidechain | |
| Live capture (macOS) | **ScreenCaptureKit system-audio** (macOS 13+) with **BlackHole** fallback | Standalone replacement for "insert on a bus" |
| Tests | Catch2 + Python golden-file tests for the ML pipeline | |