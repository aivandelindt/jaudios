# Test fixtures

`ml/tests/goldens.json` lists the fixtures and their expected key/tempo.

Until real self-produced renders are committed (via git-lfs), `tools/fixtures/FixtureRender.cpp`
renders **synthetic** stand-ins with exact ground truth:

```bash
cmake --build build --target fixtures   # renders only files that are missing
```

Each synthetic render writes `<name>.truth.json` next to the WAV (bpm, key, first downbeat,
chord per bar). An existing WAV is never overwritten, so dropping a real render in place makes
every test and the CLI use it instead.

| Fixture | Synthetic content |
|---|---|
| house_124_Amin | 32 bars, 4-on-floor, offbeat hats + bass, stabs: Am F C G |
| techno_132_Fmin | 32 bars, 0.35 s pre-roll, 16th hats, rolling bass, pad: Fm Fm Bbm Fm |
| pop_100_Cmaj | 24 bars, kick 1+3, snare 2+4, chords + arpeggio: C G Am F |
| loop_8bar_Gmaj | 8 bars (16.5 s), G C Em D |
| stem_bass_Dmin | 16 bars bass only @ 96 BPM: Dm Bb F C |
