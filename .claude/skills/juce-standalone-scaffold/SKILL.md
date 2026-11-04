---
name: juce-standalone-scaffold
description: Create/maintain the JUCE 8 CMake standalone app, audio device setup, threading model, and CLI harness.
---
## When to use
Starting the project, adding a module to CMake, or changing the audio/threading backbone.

## Steps
1. `juce_add_gui_app(App ...)` with `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`.
2. Static libs: engine_analysis, engine_theory, engine_generators, engine_synth, engine_fx, engine_export.
3. `AudioEngine` owns `juce::AudioDeviceManager`; graph = [Source stems] + [Chord synth] + [Bass synth]
   + [Drums] → per-channel gain/mute/solo → FX → master. Use `juce::AudioProcessorGraph`.
4. Threads: Audio (RT) / Analysis (ML, `juce::ThreadPool`) / UI. Communicate with lock-free FIFOs
   (`juce::AbstractFifo`) and `juce::AsyncUpdater`.
5. Add `tools/cli` target: `app-cli analyze <wav>` prints key, stems path, chords JSON. All features
   must work headless before UI.
6. Enable `-fsanitize=address,undefined` in Debug.

## Acceptance
- Plays a WAV through the graph with < 10 ms latency at 128 samples.
- `tools/rt_check.sh` (RTSan or juce::ScopedNoDenormals + custom allocator trap) reports zero RT allocations.
