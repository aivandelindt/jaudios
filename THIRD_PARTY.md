# Third-party components

| Component            | Version / commit            | License    | Copyright                               | Used for                    |
|----------------------|-----------------------------|------------|-----------------------------------------|-----------------------------|
| JUCE                 | 8.0.15 (FetchContent, pinned tag) | AGPL/Commercial → **commercial license required before release** (tracked in #TBD) | Raw Material Software | App framework |
| Catch2               | v3.9.1 (FetchContent, pinned tag) | BSL-1.0 | Catch2 authors | Unit tests only, never shipped |
| ONNX Runtime         | 1.19.x                      | MIT        | Microsoft Corporation                   | Inference                   |
| Demucs (htdemucs)    | v4 weights                  | MIT        | Meta Platforms, Inc.                    | Stem separation             |
| Basic Pitch          | ICASSP 2022 model           | Apache-2.0 | Spotify AB                              | Note transcription          |
| OpenKeyScan analyzer | pinned release, see models/manifest.json | MIT | Alexander Sommer (rekordcloud) | Key detection (model + Phase A sidecar) |
| MusicalKeyCNN        | upstream of OpenKeyScan     | MIT        | a1ex90                                  | Model architecture / training code lineage |
| GiantSteps Key datasets | —                        | CC BY-NC-SA 4.0 (annotations) | Knees et al. / MTG | **Tests only, never shipped** |

## Attribution text (shown in About → Credits)
Key detection powered by OpenKeyScan (© Alexander Sommer, MIT License), based on
MusicalKeyCNN (© a1ex90, MIT License). "OpenKeyScan" is used here as attribution only;
it is not used in product naming or marketing.
