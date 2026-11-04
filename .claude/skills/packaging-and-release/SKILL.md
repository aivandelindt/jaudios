---
name: packaging-and-release
description: Build, sign, notarize and package the app for macOS and Windows, including the Phase A OpenKeyScan sidecar.
---

## macOS
1. CMake install → `App.app`. Models (from `make models`) go to `Contents/Resources/models/`.
2. **Phase A sidecar (M1 only):** copy `build/sidecar/openkeyscan-analyzer/` to
   `Contents/Resources/sidecar/openkeyscan-analyzer/`. It is a PyInstaller bundle (~780 MB) with
   many `.so`/`.dylib` files and nested frameworks.
3. Signing order (hardened runtime, Developer ID Application):
   a. `find Contents/Resources/sidecar -type f \( -name "*.so" -o -name "*.dylib" \) -exec codesign
      --force --options runtime --timestamp --sign "$ID" {} \;`
   b. Sign the sidecar main executable with entitlements `sidecar.entitlements`:
      `com.apple.security.cs.allow-jit` = false,
      `com.apple.security.cs.allow-unsigned-executable-memory` = **true** (required by PyTorch/
      CPython in a hardened-runtime context; drop this entitlement in M2 when the sidecar is removed),
      `com.apple.security.cs.disable-library-validation` = true.
   c. Sign the main app bundle last with the app entitlements (no disable-library-validation).
   d. `codesign --verify --deep --strict` must pass before notarization.
4. Notarize (`notarytool submit --wait`), staple, verify with `spctl -a -vv`.
5. Smoke test on a clean machine: app launches, sidecar reaches "ready" < 5 s, key test fixture
   returns 8A, Gatekeeper shows no prompt.
6. DMG via create-dmg; sha256 published alongside.

## Size budget
- M1 (with sidecar): ≤ 1.1 GB installed. M2 (no sidecar): ≤ 400 MB installed. CI fails if exceeded.

## M2 removal checklist
- Remove sidecar copy steps, `allow-unsigned-executable-memory`, `disable-library-validation`,
  manifest `openkeyscan_sidecar`, `KeyDetectorSidecar` sources, and the `KEY_BACKEND=sidecar` option.
