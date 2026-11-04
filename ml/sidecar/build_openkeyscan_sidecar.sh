#!/usr/bin/env bash
# Builds the Phase A OpenKeyScan sidecar (PyInstaller bundle) from the commit pinned in
# models/manifest.json, for the platform this runs on (PyInstaller cannot cross-compile).
#
#   ml/sidecar/build_openkeyscan_sidecar.sh [build-dir]
#
# Output: <build-dir>/sidecar/openkeyscan-analyzer-<platform>.zip  (+ its sha256 for the manifest)
# Code signing / notarization is NOT done here: see the packaging-and-release skill.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="$(cd "${1:-$ROOT/build}" 2>/dev/null && pwd || (mkdir -p "${1:-$ROOT/build}" && cd "${1:-$ROOT/build}" && pwd))"
MANIFEST="$ROOT/models/manifest.json"
SRC="$ROOT/ml/third_party/openkeyscan-analyzer"
VENV="$BUILD_DIR/sidecar/venv"

read_manifest() { python3 -c "import json,sys; m=json.load(open('$MANIFEST')); print(eval(sys.argv[1]))" "$1"; }
REPO="$(read_manifest "m['openkeyscan_source']['repo']")"
COMMIT="$(read_manifest "m['openkeyscan_source']['commit']")"
CKPT_SHA="$(read_manifest "m['openkeyscan3_pt']['sha256']")"
PINS="$(python3 -c "import json; p=json.load(open('$MANIFEST'))['openkeyscan_sidecar']['build']['pins']; print(' '.join(f'{k}=={v}' for k,v in p.items() if k!='torch'))")"
TORCH="$(read_manifest "m['openkeyscan_sidecar']['build']['pins']['torch']")"

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)  PLAT=darwin-arm64;  export TARGET_ARCH=arm64 ;;
    Darwin-x86_64) PLAT=darwin-x86_64; export TARGET_ARCH=x86_64 ;;
    Linux-x86_64)  PLAT=linux-x86_64 ;;
    *) echo "unsupported platform $(uname -s)-$(uname -m)"; exit 1 ;;
esac

echo "== source $REPO @ $COMMIT"
if [[ ! -d "$SRC/.git" ]]; then
    git clone --quiet "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet --depth 1 origin "$COMMIT" || true
git -C "$SRC" checkout --quiet "$COMMIT"

GOT="$(python3 -c "import hashlib; print(hashlib.sha256(open('$SRC/checkpoints/openkeyscan3.pt','rb').read()).hexdigest())")"
[[ "$GOT" == "$CKPT_SHA" ]] || { echo "checkpoint sha256 mismatch: $GOT != $CKPT_SHA"; exit 1; }

echo "== python 3.12 venv ($VENV)"
PY="$(command -v python3.12 || true)"
[[ -n "$PY" ]] || { echo "python3.12 is required (upstream pins torch $TORCH)"; exit 1; }
[[ -x "$VENV/bin/python" ]] || "$PY" -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip
if [[ "$PLAT" == linux-* ]]; then
    # CPU-only wheel avoids ~2 GB of CUDA libraries (upstream build-linux-x64.sh does the same)
    "$VENV/bin/pip" install --quiet "torch==$TORCH" --index-url https://download.pytorch.org/whl/cpu \
        || "$VENV/bin/pip" install --quiet "torch==$TORCH"
else
    "$VENV/bin/pip" install --quiet "torch==$TORCH"
fi
"$VENV/bin/pip" install --quiet $PINS tqdm

echo "== pyinstaller ($PLAT)"
DIST="$BUILD_DIR/sidecar/dist-$PLAT"
rm -rf "$DIST" "$BUILD_DIR/sidecar/work-$PLAT"
( cd "$SRC" && "$VENV/bin/pyinstaller" --noconfirm --distpath "$DIST" --workpath "$BUILD_DIR/sidecar/work-$PLAT" openkeyscan_analyzer.spec )

INTERNAL="$DIST/openkeyscan-analyzer/_internal"
if [[ "$PLAT" == darwin-* ]]; then
    ( cd "$SRC" && "$VENV/bin/python" cleanup_video_deps.py "$DIST/openkeyscan-analyzer" ) || echo "warning: cleanup_video_deps.py failed (non-fatal)"
else
    # same pruning as upstream build-linux-x64.sh
    rm -f "$INTERNAL"/numba/np/ufunc/tbbpool.*.so "$INTERNAL"/numba/np/ufunc/omppool.*.so
    rm -rf "$INTERNAL/torch/test"
    [[ -d "$INTERNAL/torch/bin" ]] && find "$INTERNAL/torch/bin" -type f ! -name torch_shm_manager -delete
fi

ZIP="$BUILD_DIR/sidecar/openkeyscan-analyzer-$PLAT.zip"
rm -f "$ZIP"
if [[ "$PLAT" == darwin-* ]]; then
    ( cd "$DIST" && ditto -c -k --keepParent openkeyscan-analyzer "$ZIP" )
else
    ( cd "$DIST" && python3 -c "
import os, zipfile
with zipfile.ZipFile('$ZIP', 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk('openkeyscan-analyzer'):
        for f in files:
            p = os.path.join(root, f)
            info = zipfile.ZipInfo.from_file(p)
            with open(p, 'rb') as fh:
                z.writestr(info, fh.read(), zipfile.ZIP_DEFLATED)
" )
fi

echo "== built $ZIP"
echo "   size:   $(du -h "$ZIP" | cut -f1)"
echo "   sha256: $(python3 -c "import hashlib; print(hashlib.sha256(open('$ZIP','rb').read()).hexdigest())")"
echo "   pin it in models/manifest.json -> openkeyscan_sidecar.artifacts.$PLAT once it is published"
