# Changelog

## Unreleased

### Export and drag-and-drop (M9, export-and-drag-drop)
- `engine/export` (`engine_export`; no new dependency):
  - `BarGrid`: beat 0 is a bar line at or before the start of the source, with bars prepended for a pickup. Beats follow the detected
    beat grid, so tempo drift is kept.
  - `makeMidiFile`: type 1 at 960 PPQ. The conductor track has the song name, a tempo map (one event per beat that changes by
    > 0.1 %), 4/4 and the key signature; part tracks are named, with notes on the grid's ticks.
  - `writeWav`: 24-bit or float, at the source rate. `alignToGrid` pads the prepended bars.
- `app/core/ExportService`:
  - `renderPart` solos a part (the mix, chords, bass, drums, a stem) and renders it offline through the engine graph, with
    instruments and FX, plus a tail after the transport stops.
  - `DragSession`: a per-run temp folder for dragged files.
- `app/gui/ExternalDrag`: `dragFilesOut()` wraps JUCE's external file drag, for the UI shell to call.
- Project file v2 (`app/core/Project`): the source path and SHA-256, the key, BPM, beats and downbeats, the progression and chords,
  the bass and drum patterns, the instrument slots (still "slots", so version 1 readers and files keep working), the FX state of each
  channel with a stage on, and the exported assets. `audition --save-project` writes it, and `--project` restores the FX too.
- `app-cli export`: WAVs, per-part and all-parts MIDI, and the project file. It takes `--parts`, `--wav 24|float`, `--tail`,
  `--no-wav`, `--name` and every `audition` option.
- `tools/check_export_alignment.py` (stdlib) reads an export back as a DAW would: tempo, 4/4, key, track names, kicks on the 1/16
  grid and on beats, and every kick's onset in the drums WAV at its MIDI time. On the house fixture the worst onset is 0.17 ms off;
  with a 0.6 s pickup in front, bar 1 is prepended and the first downbeat is tick 3840, also within 0.18 ms. macOS CI runs both.
- Tests: the grid (pickup, on-time start, drift, no beats); key signatures; MIDI read back; the tempo map against a drifting grid; WAV
  formats; exported kicks on bar lines in ticks and samples; soloed renders summing to the mix within 1e-4 (the duck keyed by drums
  that aren't soloed); reverb tails; project v2 round trip with FX; reading version 1 files.
- Fix: the drum sampler's round-robin carried over between renders, so every export started on a different variation. `prepare`
  now resets it, and voices too, so a render from the start is reproducible.

### Insert FX (M8, fx-chain)
- `engine/fx` (`engine_fx`, JUCE DSP; no new dependency). `FxChain` runs six stages, in this order:
  - **Filter:** a TPT state-variable filter computing low-, band- and high-pass together, so changing the type crossfades.
  - **Saturation:** tanh soft clip at unity small-signal gain.
  - **OTT-style compressor:** 3 bands split at 88.3 Hz and 2.5 kHz (Linkwitz-Riley), upward and downward per band, stereo-linked, with
    depth, time and in / out gain.
  - **Sidechain duck:** keyed by kick events or the drums stem.
  - **Ping-pong delay:** tempo-synced divisions from 1/16 to 1/2.
  - **Reverb:** JUCE's.
- Parameters come from an `AudioProcessorValueTreeState` layout (`fx::createParameterLayout()`; 25 parameters) and every continuous one
  is smoothed per sample. Switching a stage crossfades over 10 ms, and a stage that is off costs nothing. A delay-time change
  crossfades between the old and the new tap, so there's no pitch glide. Turning OTT on lets its crossovers settle for 20 ms before
  the crossfade.
- Engine: each channel strip owns a parameter tree (state saved as XML) and a chain. The drums clip player outputs a kick-trigger
  channel (GM note 36), which is routed with the drums stem into every strip as the ducker's key. `AudioEngine::fxState()`,
  `setFxParameter()` and `setTempo()` are the API.
- Tests:
  - every stage's behaviour;
  - everything off is bit-transparent;
  - the zipper acceptance: each of the 25 parameters is swept the worst way (alternating extremes, then a new value every block), and
    the energy above 6 kHz is compared with five static settings. The worst sweep is -70 dB re the tone; a negative control shows an
    unsmoothed step at -31 dB;
  - in the engine: the drums clip ducks another channel (even with the drums muted), the drums stem ducks a stem, the delay follows
    the engine tempo, the state round-trips;
  - `[rt]`: the chain, and the graph with every stage on every channel while being automated. Zero allocations.
- `app-cli audition --fx CHANNEL.PARAM=VALUE` (repeatable) and `--list-fx`. Audition passes the detected BPM to the engine.
- Found by the zipper test and fixed:
  - Delay / reverb switched on: the send now fades in with the switch, and the reverb tail fades out with it.
  - OTT: the input gain is applied after the band split. A gain ramp's corners in the near-silent high band were lifted +24 dB by the
    upward compression.
  - The kick trigger is 1.0 per kick, so depth means depth (velocity had scaled it).

### VST3 hosting (M7b, synth-and-vst3-host)
- JUCE's VST3 host on (`JUCE_PLUGINHOST_VST3`; the VST3 SDK 3.8.0 inside JUCE is MIT). No new dependency.
- Out-of-process scanning: `jaudios-plugin-scanner <plugin> <out.xml>` scans one file. `PluginHost::scan()` runs it per file with a
  timeout, so a plugin that crashes or hangs only fails its own entry. The known list persists as XML
  (`~/Library/Application Support/JAudios/plugins.xml`, `JAUDIOS_PLUGIN_LIST`).
- `PluginInstrument`: a hosted plugin as a channel instrument.
  - Stereo out, inputs disabled, MIDI through a buffer reserved in prepare. The wrapper allocates nothing on the audio thread
    (`[rt]` test).
  - All-notes-off via an atomic flag; parameters by name or ID; state get / set.
  - It keeps the description it was created from: a live VST3 reports its inner binary, which the format can't reopen.
- `InstrumentSlot` / `ProjectInstruments`: a project file (`{"version":1,"slots":{...}}`) keeps each channel's instrument. That is a
  built-in preset, an SFZ file, the drum kit, or a plugin with its base64 state.
- `app-cli plugins scan [--path DIR]... | list`. `app-cli audition` gains `--chords-plugin` / `--bass-plugin`, `--chords-param` /
  `--bass-param NAME=0..1`, `--project`, `--save-project` and `--plugin-list`. A restored project renders bit-identically.
- An in-repo test instrument (`tests/plugins/TestSynth`, JUCE, sine voices; Gain and Octave params; never shipped). Tests cover:
  - the scan, including a broken `.vst3`, and list persistence;
  - pitch, and a parameter change being heard;
  - the state round trip through a project file in a fresh host;
  - playback on the chords channel in the engine graph.

  macOS CI scans it, auditions with it, and restores it in a second process.
- Fix: macOS arm64 builds of sfizz no longer get 32-bit ARM flags (`-mfpu=neon`, rejected by clang).

### Built-in instruments + generated channels in the engine (M7a, synth-and-vst3-host)
- sfizz 1.2.3 (BSD-2, stack table; approved) via FetchContent, static, apps/tests off. It builds as C++17 inside its own scope (GCC rejects
  its hex-float literals under its default C++14; C++20 turns its `u8""` literals into `char8_t`); our targets stay C++20.
- `engine/synth/presets/*.sfz`: our own patches on sfizz's oscillators (no samples): piano + electric keys (keys), warm pad (pad), pluck
  (pluck), sub bass + saw bass (bass); embedded at configure time; `sfzCategories()` for the preset browser.
- `synth::Instrument` interface; `SfzInstrument` (sfizz; note on / off, pitch wheel, CC, all-sound-off; sample-accurate) and
  `DrumInstrument` (the M6 sampler; rebuilds its kit for the device rate).
- Engine: `ClipPlayerProcessor` per generated channel (chords, bass, drums) -> strip -> master. Clips and instruments swap through the
  lock-free ownership protocol; `AudioEngine::play() / stop() / seek()` drive the source and all clip players together (sample-accurate
  in step); instruments are prepared off the audio thread. Bypass mutes the generated channels and the stems and passes the source
  through, leaving mutes / solos as they were (so leaving bypass restores them). `[rt]` tests: SFZ + drums instruments and the full graph
  with generated channels render with zero allocations; `tools/rt_check.sh` PASS.
- `app-cli audition <file> [--style] [--chords] [--bass] [--no-drums] [--stems] [--bypass] [--out FILE.wav]`: generates chords, bass and
  drums for the file and renders source + parts through the engine graph (29-118x realtime on the fixtures); `--list-presets`.
- Fixed while testing: an offline render that sets mixer state after preparing ramped into it over 20 ms (bypass leaked the first
  chord). The CLI sets the state first; prepare snaps the strips to it (regression test). The GUI's play / stop / seek now drive the
  whole transport.

### Drum generator (M6)
- Grooves: `engine/generators/drums/*.json` against `drums/schema.json` (house four-on-the-floor, minimal house 4-bar,
  techno, trance, pop backbeat, lo-fi with swing + ghosts, R&B shuffle, cinematic toms): 1/16 lanes for kick / snare / clap /
  hat / open hat / perc / ride over 2-8 bars, plus one-bar fill, riser_hit, drop and half_time variants. Embedded at configure
  time; `tools/validate_patterns.py` now checks both libraries (`$ref` support, drum lane lengths).
- `generators::generateDrums()`: the groove on the detected bars (steps subdivide each bar, so tempo drift is followed; step 0 is
  the downbeat itself). Options: kick on/off, transitions on the last bar of every phrase (a riser-hit lands an accented kick + open
  hat on the next downbeat), intensity 1-5 (elements, ghost notes, velocity), swing.
- `engine/synth/DrumSampler`: JAudios' own synthesised kit (no third-party samples): 3 velocity layers x 3 round robins per element;
  user kits from a folder of `<element>[_v<n>][_rr<n>].wav` files (resampled; missing elements stay synthesised); a
  `juce::Synthesiser` with one-shot voices, round robin, velocity layers, closed-hat choke and per-element pan; allocation-free
  `renderNextBlock` (`[rt]` test); offline `renderDrums()` and GM `drumsToMidi()`.
- `app-cli drums <file> [--style] [--pattern] [--intensity] [--no-kick] [--transition] [--phrase] [--swing] [--kit DIR] [--split]
  [--out DIR]`: `drums.wav` + `drums.mid`, or one WAV + MIDI per element.
- Acceptance (skill: kicks within 5 ms of the detected downbeats): exact in the MIDI (0.000 ms) and within 5 ms in the rendered audio
  for every pattern on house, techno, pop and the loop; kick onsets in the sampler output are sample-accurate (< 1 ms).
- Not yet: the drums channel in the live engine graph (playback in the app comes with the synth / UI milestones).

### Bass generator (M5)
- Patterns: `engine/generators/patterns/*.json` (pop root, pop 8ths, house offbeat, techno rolling 1/16, trance rolling,
  syncopated funk (2 bars, swing), walking, whole notes) against `patterns/schema.json`; embedded at configure time.
  `tools/validate_patterns.py` (stdlib) checks the schema keywords plus loop bounds, overlaps and unique names; ctest + macOS CI.
- `generators::generateBass()`: steps take pitch from their role (root / third / fifth / octave / chord / scale / approach)
  on the chord sounding then; root on every chord's downbeat; passing and approach tones are scale tones (so every note is a
  chord or scale tone, skill acceptance); octave leaps capped at 20 % of the intervals; notes within a third of a low vocal
  note (< E3) move an octave or fall back to the root; per-step velocity and gate, grid 1/8..1/32 and swing overrides,
  1/2/4-bar loops; sidechain flag when a note starts on or holds over a kick.
- `analysis::detectKicks()`: 4th-order low-pass (110 Hz) energy rise with adaptive, relative and absolute floors. On the fixture
  mixes recall is 1.0; precision 1.0 on house and the loop, 0.85 on techno and 0.44 on pop, where bass attacks between the
  kicks share the band. On the pop fixture's HT-Demucs drums stem it is exact (14 / 14), which is the path `--stems` uses.
- `app-cli bass <file> [--style] [--pattern] [--stems] [--grid] [--swing] [--out FILE.mid]`, `app-cli bass --list-patterns`.
  The CLI's harmony pipeline (key, beat grid, evidence, suggestions) is shared by `chords` and `bass`.

### Chord engine (M4)
- `theory::Chord`: maj / min / dim / aug / sus2 / sus4 / 7 / m7 / maj7 / add9, inversions, names spelled for the key
  signature ("C/E", "Bb", "C#m"), Roman numerals relative to the key's own scale (borrowed bVI / bVII / iv; harmonic-minor
  V), close / open / drop-2 voicings, `voiceLead()` (nearest chord tones, keeps root / third / 7th first) and
  `voiceLeadingCost()` (minimum pitch-class movement).
- `generators::ChordEngine`: weighted templates for pop, house, techno, trance, lo-fi, R&B and cinematic in major and minor
  (with modal mixture); harmonic rhythm 1 / 2 per bar or 1 per 2 bars on the downbeat bars; score = w1 melody consonance +
  w2 bass agreement + w3 chroma match + w4 template prior - w5 voice leading (per slot), the prior scaled down when the key
  margin is low. Candidates: every template at every rotation (inversion from the bass) plus an exact Viterbi fit over the
  key's vocabulary. Top 8, deterministic given the seed; six ranked alternatives per slot; rendering with voice leading,
  voicing, octave and seeded velocity humanize.
- `app/core/HarmonySource`: analysis + transcription -> engine evidence. `app-cli chords <file> [--style] [--rhythm]
  [--sevenths] [--stems] [--voicing] [--humanize] [--top] [--seed] [--out FILE.mid]`.
- Results: top-1 matches every bar of the truth chords on pop (C G Am F), house (Am F C G), techno (Fm Fm Bbm Fm) and the
  loop (G C Em D), also where key detection chose the relative key; C major fixture: only I / IV / V / vi, voice leading
  <= 6 (skill acceptance). A key change re-renders in <= 3.4 ms for 32 bars (target < 50 ms).

### Audio to MIDI (M3)
- Model: Basic Pitch (Spotify, Apache-2.0) uses upstream's own `nmp.onnx` from the pinned basic-pitch 0.4.0 wheel,
  byte-for-byte. `ml/fetch_basic_pitch.py` (stdlib only) checks the wheel's and the model's sha256;
  `tools/models.py --only basic_pitch`. 230 KB, same bytes on every machine, so the app accepts only the manifest sha256.
- `engine/analysis/transcription`: `AudioToMidi` reproduces `basic_pitch.inference.run_inference` (mono mean, 22.05 kHz,
  2 s windows with 30 overlapping frames, batched through ORT CPU, unwrapped and trimmed like upstream). `extractNotes()`
  ports `output_to_notes_polyphonic` + `get_pitch_bends` (inferred onsets, peak picking, energy tracking, melodia
  trick) and `frameToSeconds()` ports `model_frames_to_time`, upstream quirks included. Defaults from the skill
  (frame 0.5, onset 0.5, min 58 ms) in `NoteParams`. Output: notes with per-note confidence and a
  `juce::MidiMessageSequence` (velocity, pitch bends like `note_events_to_midi`, plus a wheel re-centre after each bent note).
- Golden vs upstream (`jaudios_transcription_tests`, references from `ml/tests/basic_pitch_reference.py`): posteriors
  max |d| 1.2e-07 (threshold 1e-4); note creation on upstream's posteriors gives exactly upstream's notes, bends and times
  for both the skill's and upstream's parameter sets on melody / bass / pop; from the WAV with our resampler, F1 1.0 vs
  upstream's notes (20 ms).
- Acceptance (skill: note F1 >= 0.80, 50 ms): 0.992 on the new synthetic sung melody fixture `melody_mono_Emin` (60
  notes, vibrato). This is a synthetic stand-in, not a real vocal test set. Fixtures: `FixtureRender` adds
  `melody_mono_Emin` and writes note-level truth (`notes`) for it and for `stem_bass_Dmin`.
- `quantize()`: snaps note starts / ends to a grid subdividing every beat of the tempo-and-beatgrid beats (default
  1/16, strength, follows tempo drift, extrapolates outside the grid). Swing preservation: the median delay of off-step
  notes minus that of on-step notes becomes the swing of the target grid, so latency is not mistaken for swing.
- `makeMonophonic()` for bass (keeps the higher-energy note where notes overlap, trims or drops the others).
- `app-cli transcribe <file> [--stem mix|drums|bass|other|vocals|guide] [--mono|--poly] [--quantize ...] [--onset/--frame/--min-ms]
  [--out FILE.mid]`: JSON notes + a format-1 MIDI file at the detected tempo; stems come from the stem cache or HT-Demucs.
  The loop's bass stem gives its 32 quarter-note roots (G2..E3).
- Known: on the drum-less melody fixture the beat grid sits ~20 ms late, so 1/16 quantising moves onsets from a 4.9 ms to a
  23.3 ms median error vs the truth (F1 unchanged at 0.992). That bias is the beat tracker's (tempo-and-beatgrid).
- macOS CI: fetches the model, installs the reference pins, runs `jaudios_transcription_tests` and `app-cli transcribe`.
- Docs: docs/building-macos.md section 3c; restores the "4. Run" heading the M2 edit dropped.

### Stem separation (M2)
- `ml/export_demucs.py`: exports the network core of upstream HT-Demucs (demucs 4.1.0, `htdemucs`, MIT) to ONNX
  (opset 17, static 7.8 s segment: `mix` (1,2,343980) + `spec` (1,4,2048,336) -> `spec_out` (1,4,4,2048,336) +
  `wave_out` (1,4,2,343980)). The complex STFT/ISTFT stay outside the graph (ONNX has no complex tensors); the
  wrapper swaps upstream's four complex helpers and runs upstream `forward()` unchanged, so the architecture is
  not re-implemented. Traced with grad enabled so `nn.MultiheadAttention` avoids its non-exportable fused path.
- Parity vs upstream PyTorch forward on house/pop/techno segments: max |d| 4.8e-04 (threshold 1e-3), SNR >= 83 dB.
  ORT CPU: ~2.3 s per segment on 4 x86-64 vCPUs. Writes `htdemucs.onnx.provenance.json`; `tools/models.py
  --only demucs [--python ...]` exports / verifies it (same acceptance rule as the key model).
- `engine/analysis/stems`: `DemucsSpec` ports HTDemucs `_spec`/`_magnitude` and `_mask`/`_ispec` (torch.stft /
  istft semantics: reflect padding, periodic Hann, normalized, Nyquist bin and 2 edge frames dropped) to C++ on
  juce::dsp::FFT; vs torch max |d| 5.7e-06 (forward) and 1.4e-06 (inverse). `StemSeparator` reproduces
  `demucs.api.Separator(shifts=0)`: mono-reference normalisation, `apply_model` segmenting (7.8 s, 25 % overlap,
  centred padding of the short last chunk with real audio, triangular cross-fade), ORT CPU per segment, progress
  callback, `cancel()` (stops inside the running inference via RunOptions terminate), mono / non-44.1 kHz input.
  Stems `{drums, bass, other, vocals}`, input length, sample-aligned. Model accepted by manifest sha256 or verified
  provenance (`verifyDemucsExport`). `findModelFile()` shares the key model's search path.
- Golden (`jaudios_stems_tests`, references from upstream PyTorch via `ml/tests/demucs_reference.py`): SNR vs
  `apply_model` 86.9 dB (16.5 s loop, 3 segments) and 88.9 dB (3 s clip, one padded segment); null test (sum of
  stems vs mix) -46.7 / -41.1 dBFS RMS, the same as upstream's own output (skill threshold -40 dBFS).
- `app-cli separate <file> [--out DIR]`: stems as 32-bit float WAVs + JSON report (segments, time, realtime factor,
  null residual, per-stem RMS). house_124_Amin (62 s): 11 segments in 29 s on 4 x86-64 vCPUs, null -44.1 dBFS.
- `StemCache`: stems cached in `~/Library/Application Support/JAudios/cache/stems/<sha256>/` (raw float32 + meta),
  key = SHA-256 of the decoded audio + `StemSeparator::cacheId()` (model sha256 + settings); atomic writes (temp dir
  + rename), corrupt/truncated entries are misses, LRU `prune()` to 4 GiB. `app-cli separate` uses it
  (`--no-cache`, `JAUDIOS_STEM_CACHE_DIR`); a second run of the loop is a hit (0 segments).
- Guide stems: `GuideSelection` (default `other+vocals`, parse/print `bass+vocals`, `all`) and `mixGuide()`;
  `app-cli separate --guide ...` writes `guide.wav`.
- Engine: `SourcePlayerProcessor` plays the source and its stems on one transport (10 output channels: source +
  4 stereo stem layers; stems swapped in through the existing lock-free ownership protocol). Four stem strips
  (`ChannelId::stemDrums..stemVocals`, `stemChannel()`) with gain / mute / solo into the master; the source is
  muted while stems are loaded unless soloed. `AudioEngine::setSource()` / `setStems()`; a new source drops its
  stems. `[rt]` test: zero allocations while playing stems (`tools/rt_check.sh` PASS).
- macOS CI: exports HT-Demucs on the Mac (parity vs PyTorch), runs the stem goldens, a CoreML golden
  (`[!mayfail]`) and a 3-minute separation with its timing and null test; docs/building-macos.md section 3b.

### macOS (Apple Silicon)
- Fix: on macOS the ONNX Runtime dylib was staged as `libonnxruntime.dylib`, but its install name is
  `@rpath/libonnxruntime.1.19.2.dylib`, so every binary using it would have failed to launch ("Library not
  loaded"). It is now staged and bundled under its install name.
- Fix (found by the first macOS CI run): `rt::isTrapInstalled()` used a weak undefined symbol, which Apple's linker
  rejects ("Undefined symbols: _jaudios_rt_trap_installed"), so `app-cli` and `jaudios_tests` did not link on macOS.
  The trap now registers itself from a static initializer in RealtimeTrapHooks.cpp.
- CI: `.github/workflows/macos.yml` builds and tests on GitHub's `macos-15` arm64 runners (Release build, ML env
  from the manifest pins, ONNX export, full ctest incl. CQT/ONNX goldens, app bundle checks, CLI run, rt_check).
- CI: the key model is exported once on Linux (reproducing the manifest sha256) and handed to the macOS job as the
  `openkeyscan3-onnx` artifact, so macOS tests the shipped file. Exporting on macOS arm64 currently segfaults
  (second CI run); the macOS job keeps a non-fatal export attempt with faulthandler for diagnosis.
- ml/export_openkeyscan.py runs its three stages in separate processes (torch export + reference logits;
  onnx/onnxsim simplify; onnxruntime parity), so torch never shares a process with the onnx / onnxsim /
  onnxruntime native libraries; addresses the macOS arm64 export segfault. Output unchanged on Linux (same sha256).
  `ml/tests/export_probe.py` reports which library combination crashes, one process per case.
- Fix (found by the macOS export probe): onnxsim 0.6.5 segfaults on macOS arm64 even on its own. For this model it
  is a no-op (raw `torch.onnx.export` graph: same 23 ops, bit-identical weights), so when the simplify stage dies on
  a signal the export validates and uses the raw graph; parity is still checked; provenance records
  `"simplified": false`. On Linux nothing changes (onnxsim still runs, same sha256).
- Model acceptance: exports are not byte-reproducible across machines (onnxsim folds constants with CPU-specific ORT
  kernels; a GitHub Linux x86-64 runner produced a different sha256 with the same pins). `ml/export_openkeyscan.py`
  now writes `openkeyscan3.onnx.provenance.json`; `KeyDetectorOnnx` and `tools/models.py` accept a model whose sha256
  differs from the manifest only if that record matches the file, the pinned upstream commit + checkpoint, and a
  passing parity check (< 1e-4). The app logs "verified local export from <platform>"; `modelOrigin()` reports it.
- CI: the macOS job exports the model on the Mac and requires the app to load it (backend onnx).
- Fix (found by the macOS golden test): the CoreML execution provider returned wrong logits (off by 3-5, different
  key on 2 of 5 fixtures) with ONNX Runtime 1.19.2, running only 14 of 23 nodes. `KeyDetectorOnnx` now uses the CPU
  provider on macOS too; `JAUDIOS_ONNX_COREML=1` opts in for investigation; `[onnx][coreml][!mayfail]` tracks it.
- Docs: `docs/building-macos.md`.
- Test: the ONNX session-creation timing is measured after one warm-up construction (the first one also pays for
  loading the dylib and a cold disk cache, which flaked right after a rebuild).

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
