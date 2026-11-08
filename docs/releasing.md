# Releasing JAudios (macOS, Apple Silicon)

A release is a DMG with a signed (and, when the secrets exist, notarised) `JAudios.app`, plus its
`.sha256`. `tools/package_macos.sh` builds it; the macOS workflow runs it on every push and PR, and
on a `v*` tag it also attaches the DMG to a GitHub release.

## What the package contains

| Path in `JAudios.app` | What |
|---|---|
| `Contents/MacOS/JAudios` | the app |
| `Contents/MacOS/jaudios-plugin-scanner` | the out-of-process VST3 scanner (a crashing plugin can't take the app down) |
| `Contents/Frameworks/libonnxruntime*.dylib` | ONNX Runtime |
| `Contents/Resources/models/` | `openkeyscan3.onnx`, `basic_pitch.onnx`, `htdemucs.onnx` and their provenance records, whichever fit the 200 MB cap (about 193 MB together) |

The Phase A key sidecar is not shipped (the ONNX key model replaced it). Models are looked up in
`~/Library/Application Support/JAudios/models/` first, so a user can install newer ones there, then
in the bundle. The packaging fails if the bundled models exceed 200 MB or the installed app 400 MB.

## Build a release locally

```bash
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DJAUDIOS_DEV_PATHS=OFF
```

```bash
cmake --build build-release
```

`-DJAUDIOS_DEV_PATHS=OFF` drops the build-tree model and sidecar paths from the binary. They are
only used when the executable runs from inside its build tree anyway, but a release need not carry
them. Export or copy the models (see [building-macos.md](building-macos.md)), then:

```bash
tools/package_macos.sh --build-dir build-release --models build/models --out dist
```

Without a signing identity the app is ad-hoc signed: it runs on the Mac that built it, and
Gatekeeper asks on others. With a Developer ID certificate in your keychain:

```bash
MACOS_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" NOTARY_KEYCHAIN_PROFILE=jaudios tools/package_macos.sh --build-dir build-release --models build/models --out dist
```

Create the notarytool profile once with
`xcrun notarytool store-credentials jaudios --apple-id ... --team-id ... --password <app-specific password>`.

## Smoke test

The packaged app has a headless self-test, which CI runs on the app copied out of the DMG, with a
fresh `HOME` so that only the bundled models can be found:

```bash
dist/stage/JAudios.app/Contents/MacOS/JAudios --self-test tests/fixtures/house_124_Amin.wav
```

It prints JSON (version, the model files found, the key backend, the plugin scanner, and the key,
tempo and progression of the file) and exits 0 when the ONNX key model and Basic Pitch were found,
the scanner exists and the file analyses.

## Signing (what the script does)

- **Inside-out order:** the dylibs, then the scanner (`packaging/macos/PluginScanner.entitlements`),
  then the app (`packaging/macos/JAudios.entitlements`). Then `codesign --verify --deep --strict`.
- **Hardened runtime and a secure timestamp:** `--options runtime --timestamp`, when the identity is
  a real one.
- **Entitlements:**
  - `device.audio-input`: the microphone and input devices, for live capture;
  - `cs.disable-library-validation`: needed to load third-party VST3 plugins, which are signed by other
    teams. ScreenCaptureKit needs no entitlement; macOS asks for the Screen & System Audio Recording
    permission on first use.
- **Notarisation:** `notarytool submit --wait`, then `stapler staple` and an `spctl` check.

## CI secrets (repository Settings → Secrets and variables → Actions)

| Secret | Value |
|---|---|
| `MACOS_SIGN_IDENTITY` | `Developer ID Application: Your Name (TEAMID)` |
| `MACOS_CERT_P12_BASE64` | the exported certificate and private key: `base64 -i cert.p12` |
| `MACOS_CERT_PASSWORD` | the .p12's password |
| `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`, `NOTARY_PASSWORD` | the Apple ID, team ID and an app-specific password for notarisation |

Without them CI still packages and smoke-tests an ad-hoc signed DMG. The certificate is imported
into a temporary keychain, which is deleted when the script exits.

## Cut a release

1. Bump `project(JAudios VERSION x.y.z)` in `CMakeLists.txt` and move the CHANGELOG's "Unreleased"
   section under the version.
2. Merge, then tag and push:

```bash
git tag v0.1.0 && git push origin v0.1.0
```

The workflow builds, tests, packages and smoke-tests, and the `release` job creates the GitHub
release with `JAudios-<version>.dmg` and `JAudios-<version>.dmg.sha256`. Users can check the download
with `shasum -a 256 -c JAudios-<version>.dmg.sha256`.

Windows packaging is not covered yet.
