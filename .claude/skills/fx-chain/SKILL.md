---
name: fx-chain
description: Per-channel insert FX: filter, delay, reverb, OTT-style multiband, sidechain, saturation.
---
## Steps
- `juce::dsp` ProcessorChain: StateVariableTPT filter, ping-pong delay (tempo-synced), Reverb (JUCE or FDN), 3-band upward/downward compressor (OTT style), sidechain ducker keyed by drum/kick events or source stem, soft-clip saturation. All parameters via `AudioProcessorValueTreeState`, smoothed.

## Acceptance
- No zipper noise sweeping any parameter (spectral test).
