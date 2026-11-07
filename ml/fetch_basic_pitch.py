#!/usr/bin/env python3
"""Fetch Basic Pitch's ONNX model (Spotify, Apache-2.0) as pinned in models/manifest.json.

    python3 ml/fetch_basic_pitch.py [--out build/models/basic_pitch.onnx] [--wheel PATH]

Basic Pitch 0.4.0 ships its own ONNX export of the ICASSP 2022 model (nmp.onnx) inside the PyPI
wheel. We use that file byte-for-byte rather than re-exporting from TensorFlow: same bytes on every
machine, nothing to drift. Steps: download the wheel (or use --wheel), check its sha256, extract
the one member, check the model's sha256, write it atomically. Stdlib only.
"""
import argparse, hashlib, io, json, sys, tempfile, urllib.request, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENTRY = json.loads((ROOT / "models" / "manifest.json").read_text())["basic_pitch_onnx"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "models" / ENTRY["file"]))
    ap.add_argument("--wheel", help="local copy of the pinned wheel (skips the download)")
    args = ap.parse_args()
    src = ENTRY["source"]

    data = Path(args.wheel).read_bytes() if args.wheel else urllib.request.urlopen(src["wheel_url"], timeout=120).read()
    got = hashlib.sha256(data).hexdigest()
    if got != src["wheel_sha256"]:
        sys.exit(f"wheel sha256 mismatch: {got} != {src['wheel_sha256']}")
    model = zipfile.ZipFile(io.BytesIO(data)).read(src["member"])
    got = hashlib.sha256(model).hexdigest()
    if got != ENTRY["sha256"] or len(model) != ENTRY["size"]:
        sys.exit(f"model sha256 mismatch: {got} != {ENTRY['sha256']}")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=out.parent, delete=False) as f:
        f.write(model)
    Path(f.name).replace(out)
    print(f"{out}  size={len(model)}  sha256={got}  (basic-pitch {src['version']}, {src['license']})")


if __name__ == "__main__":
    main()
