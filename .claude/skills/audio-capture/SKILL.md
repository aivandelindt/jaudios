---
name: audio-capture
description: Import files and capture live system/DAW audio into a buffer for analysis (standalone replacement for a plugin insert).
---
## Steps
1. File import: drag-and-drop + open dialog; WAV/AIFF/FLAC/MP3/M4A via `juce::AudioFormatManager` (+ CoreAudio formats on macOS). Resample everything to 44.1 kHz float, keep original SR in metadata.
2. Live capture (macOS 13+): ScreenCaptureKit `SCStream` with `capturesAudio = true`, app-filtered (user picks DAW process). Write Objective-C++ bridge in `/app/platform/mac/`. Fallback: any input device (BlackHole/aggregate device) via AudioDeviceManager.
3. Windows: WASAPI loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`).
4. Record button → ring buffer → on stop, hand `CaptureResult {buffer, sr, startTimeMs}` to Analysis thread.
5. Detect tempo + downbeat (see key-and-tempo-analysis) and show bar grid in the capture waveform.
6. No capture length limit; stream to a temp file if > 5 min.

## Acceptance
- Capture from Ableton/Logic without extra drivers on macOS 13+.
- Imported MP3 and live capture produce byte-identical downstream analysis for the same source.
