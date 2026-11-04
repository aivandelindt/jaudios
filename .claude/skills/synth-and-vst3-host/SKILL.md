---
name: synth-and-vst3-host
description: Built-in sound engine and VST3 hosting for auditioning chords/bass with user instruments.
---
## Steps
1. Built-in: sfizz-backed SFZ instruments (piano, pad, pluck, sub bass, saw bass, keys). Preset browser by category.
2. VST3 host: `juce::AudioPluginFormatManager` scan (background, sandboxed scanner process to survive crashes), per-channel plugin slot, editor window, state save/restore in project file.
3. MIDI routing: Chord channel and Bass channel each → built-in OR hosted instrument.
4. Bypass mode: mute all generated channels, pass source through, restore state on exit.

## Acceptance
- Load Serum/Vital/Diva (if installed) and play generated chords; state persists across restart.
