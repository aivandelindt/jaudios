---
name: ui-shell
description: Original UI: capture waveform with bar grid, stem channels, chord lane, bass lane, drum lane, export boxes.
---
## Rules
- Original design; do not reproduce competitor layouts/colours/strings.
- Layout: top = source waveform + stems (mute/solo/gain); middle = chord lane (clickable chords → alternatives popup with piano), bass + drum lanes with accent editor; right = export tiles (drag MIDI/WAV).
- Zoom: trackpad pinch and Cmd+scroll. Keyboard: Space play, R record, Cmd+Z undo (ValueTree UndoManager).
- 60 fps repaint budget; waveform via cached thumbnails.
