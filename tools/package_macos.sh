#!/usr/bin/env bash
# Builds the release package (packaging-and-release): a signed JAudios.app inside a DMG, with its
# sha256. Run after `cmake --build` (a release build: -DCMAKE_BUILD_TYPE=Release -DJAUDIOS_DEV_PATHS=OFF).
#
#   tools/package_macos.sh [--build-dir build] [--models DIR] [--out dist]
#
# The bundle gets the plugin scanner (Contents/MacOS) and the models that fit the 200 MB cap
# (Contents/Resources/models: key, Basic Pitch, HT-Demucs with its provenance record); the Phase A
# sidecar is not shipped. Signing goes inside-out with the hardened runtime: the dylibs, the scanner
# (its own entitlements), then the app; `codesign --verify --deep --strict` must pass.
#
# Signing identity and notarisation come from the environment; without them the package is
# ad-hoc signed (runs on this Mac; Gatekeeper will ask on others):
#   MACOS_SIGN_IDENTITY        "Developer ID Application: Name (TEAMID)"
#   MACOS_CERT_P12_BASE64      the certificate (.p12, base64) and MACOS_CERT_PASSWORD: imported
#                              into a temporary keychain (CI)
#   NOTARY_KEYCHAIN_PROFILE    a notarytool keychain profile, or
#   NOTARY_APPLE_ID + NOTARY_TEAM_ID + NOTARY_PASSWORD (app-specific password), or
#   NOTARY_KEY_FILE + NOTARY_KEY_ID + NOTARY_ISSUER (App Store Connect API key)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
MODELS=""
OUT="$ROOT/dist"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD="$2"; shift 2 ;;
        --models) MODELS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        *) echo "usage: $0 [--build-dir DIR] [--models DIR] [--out DIR]" >&2; exit 2 ;;
    esac
done
MODELS="${MODELS:-${JAUDIOS_MODELS_DIR:-$BUILD/models}}"
VERSION="$(sed -n 's/^project(JAudios VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
SRC_APP="$(find "$BUILD/app/JAudios_artefacts" -maxdepth 2 -name JAudios.app -type d | head -n1)"
SCANNER="$(find "$BUILD/tools/plugin-scanner" -type f -name jaudios-plugin-scanner -perm -u+x | head -n1)"
[[ -d "$SRC_APP" ]] || { echo "error: no JAudios.app under $BUILD/app (build first)" >&2; exit 1; }
[[ -x "$SCANNER" ]] || { echo "error: no jaudios-plugin-scanner under $BUILD/tools (build first)" >&2; exit 1; }

STAGE="$OUT/stage"
APP="$STAGE/JAudios.app"
rm -rf "$STAGE"
mkdir -p "$STAGE"
ditto "$SRC_APP" "$APP"
rm -rf "$APP/Contents/Resources/sidecar" # Phase A only; the ONNX key model replaced it

echo "== bundle: plugin scanner, models"
cp "$SCANNER" "$APP/Contents/MacOS/jaudios-plugin-scanner"
mkdir -p "$APP/Contents/Resources/models"
MODEL_BYTES=0
for m in openkeyscan3.onnx basic_pitch.onnx htdemucs.onnx; do
    if [[ -f "$MODELS/$m" ]]; then
        cp "$MODELS/$m" "$APP/Contents/Resources/models/"
        [[ -f "$MODELS/$m.provenance.json" ]] && cp "$MODELS/$m.provenance.json" "$APP/Contents/Resources/models/"
        MODEL_BYTES=$((MODEL_BYTES + $(stat -f%z "$MODELS/$m")))
        echo "   $m ($(( $(stat -f%z "$MODELS/$m") / 1048576 )) MB)"
    else
        echo "   $m: not found in $MODELS (not bundled)"
    fi
done
for required in openkeyscan3.onnx basic_pitch.onnx; do
    [[ -f "$APP/Contents/Resources/models/$required" ]] || { echo "error: $required is required in the bundle" >&2; exit 1; }
done
if (( MODEL_BYTES > 200 * 1024 * 1024 )); then # AGENTS.md: never bundle more than 200 MB of models
    echo "error: bundled models are $((MODEL_BYTES / 1048576)) MB (cap 200 MB)" >&2
    exit 1
fi

echo "== signing"
IDENTITY="${MACOS_SIGN_IDENTITY:--}"
KEYCHAIN=""
if [[ -n "${MACOS_CERT_P12_BASE64:-}" ]]; then
    KEYCHAIN="$OUT/signing.keychain-db"
    KEYCHAIN_PASSWORD="$(uuidgen)"
    security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
    security set-keychain-settings -lut 3600 "$KEYCHAIN"
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
    echo "$MACOS_CERT_P12_BASE64" | base64 --decode > "$OUT/cert.p12"
    security import "$OUT/cert.p12" -k "$KEYCHAIN" -P "${MACOS_CERT_PASSWORD:-}" -T /usr/bin/codesign
    rm -f "$OUT/cert.p12"
    security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null
    security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')
    trap 'security delete-keychain "$KEYCHAIN" 2>/dev/null || true' EXIT
fi
SIGN=(codesign --force --sign "$IDENTITY")
if [[ "$IDENTITY" != "-" ]]; then
    SIGN+=(--options runtime --timestamp) # hardened runtime + secure timestamp (notarisation needs both)
else
    echo "   no MACOS_SIGN_IDENTITY: ad-hoc signature (local use; not notarisable)"
fi
# inside-out: every dylib / framework, then the helper, then the app
while IFS= read -r -d '' lib; do
    "${SIGN[@]}" "$lib"
done < <(find "$APP/Contents/Frameworks" "$APP/Contents/Resources" -type f \( -name "*.dylib" -o -name "*.so" \) -print0 2>/dev/null)
"${SIGN[@]}" --entitlements "$ROOT/packaging/macos/PluginScanner.entitlements" "$APP/Contents/MacOS/jaudios-plugin-scanner"
"${SIGN[@]}" --entitlements "$ROOT/packaging/macos/JAudios.entitlements" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

INSTALLED_MB=$(( $(du -sk "$APP" | cut -f1) / 1024 ))
echo "== installed size: ${INSTALLED_MB} MB"
if (( INSTALLED_MB > 400 )); then # skill size budget (no sidecar)
    echo "error: the app is ${INSTALLED_MB} MB (budget 400 MB)" >&2
    exit 1
fi

echo "== dmg"
ln -s /Applications "$STAGE/Applications"
DMG="$OUT/JAudios-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "JAudios $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
[[ "$IDENTITY" != "-" ]] && codesign --force --sign "$IDENTITY" --timestamp "$DMG"

NOTARIZED=false
NOTARY=()
if [[ -n "${NOTARY_KEYCHAIN_PROFILE:-}" ]]; then
    NOTARY=(--keychain-profile "$NOTARY_KEYCHAIN_PROFILE")
elif [[ -n "${NOTARY_APPLE_ID:-}" ]]; then
    NOTARY=(--apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" --password "$NOTARY_PASSWORD")
elif [[ -n "${NOTARY_KEY_FILE:-}" ]]; then
    NOTARY=(--key "$NOTARY_KEY_FILE" --key-id "$NOTARY_KEY_ID" --issuer "$NOTARY_ISSUER")
fi
if [[ ${#NOTARY[@]} -gt 0 && "$IDENTITY" != "-" ]]; then
    echo "== notarising"
    xcrun notarytool submit "$DMG" "${NOTARY[@]}" --wait
    xcrun stapler staple "$DMG"
    spctl -a -vv -t open --context context:primary-signature "$DMG"
    NOTARIZED=true
fi

(cd "$OUT" && shasum -a 256 "$(basename "$DMG")" > "$(basename "$DMG").sha256")
DMG_MB=$(( $(stat -f%z "$DMG") / 1048576 ))
cat <<JSON
{
  "version": "$VERSION",
  "dmg": "$DMG",
  "sha256": "$(cut -d' ' -f1 "$DMG.sha256")",
  "dmg_mb": $DMG_MB,
  "installed_mb": $INSTALLED_MB,
  "models_mb": $((MODEL_BYTES / 1048576)),
  "signature": "$([[ "$IDENTITY" == "-" ]] && echo ad-hoc || echo developer-id)",
  "notarized": $NOTARIZED
}
JSON
