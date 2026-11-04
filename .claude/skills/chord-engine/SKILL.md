---
name: chord-engine
description: Rule-based chord progression suggestions that fit detected key, source melody and rhythm.
---
## Model
- Represent: Key, Scale degree, Chord {root, quality (maj/min/dim/aug/sus2/sus4/7/m7/maj7/add9), inversion, bass note}.
- Progression templates per style (pop, house, techno, trance, lo-fi, R&B, cinematic): weighted lists of degree sequences (e.g. vi-IV-I-V, i-VI-III-VII, ii-V-I). Include borrowed/modal-mixture chords (bVI, bVII, iv in major).
- Harmonic rhythm: 1 chord/bar default; options 2/bar, 1 per 2 bars; align to detected downbeats.

## Scoring (fit to source)
score = w1·melodyConsonance(melody MIDI notes in bar vs chord tones)
      + w2·bassAgreement(detected bass root vs chord root)
      + w3·chromaMatch(bar chroma vs chord template)
      + w4·templatePrior(style)
      - w5·voiceLeadingCost(prev→this, semitone movement sum)
Return top-N progressions (N=8) with scores; deterministic given seed.

- Pass the key-detection softmax margin through so chord scoring can down-weight `templatePrior` when key confidence is low.

## Editing
- Replace single chord: offer 6 alternatives ranked for that slot.
- Inversion buttons, voicing (close/open/drop-2), octave, "humanize" velocity ±8.
- Piano-roll preview: play chord alone or with source.

## Acceptance
- Golden tests: given C major melody fixture, top-1 includes I/IV/V/vi only; voice-leading cost ≤ 6 semitones per change.
- Any key/mode change re-renders in < 50 ms.
