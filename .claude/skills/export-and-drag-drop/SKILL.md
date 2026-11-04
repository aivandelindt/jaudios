---
name: export-and-drag-drop
description: Export MIDI/WAV for stems, chords, bass, drums; drag directly into a DAW.
---
## Steps
1. MIDI: type-1 file, tempo map from analysis, track names, key signature meta event, bar-aligned start.
2. WAV: 24-bit/float, source sample rate, rendered offline through same graph (`AudioProcessorGraph` offline render), optional tail for reverb.
3. Drag source: `juce::DragAndDropContainer::performExternalDragDropOfFiles` with temp files in a per-session dir.
4. Project file (.json + assets): source hash, key/tempo, progression, patterns, plugin states.

## Acceptance
- Dragging into Ableton/Logic lands bar-aligned at project tempo.
