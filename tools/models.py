#!/usr/bin/env python3
"""`make models`: fetch/verify/unpack model artefacts listed in models/manifest.json. Stdlib only.

Sidecar (Phase A): for this platform's artifact
  1. if the manifest has url + sha256: download, verify, unpack to build/sidecar/openkeyscan-analyzer
  2. else if build/sidecar/openkeyscan-analyzer-<platform>.zip exists (from the build script): verify
     against the manifest sha256 when one is pinned, otherwise print its sha256 to pin; then unpack
  3. else: tell the user to run ml/sidecar/build_openkeyscan_sidecar.sh (or pass --build)
"""
import argparse, hashlib, json, os, platform, shutil, subprocess, sys, urllib.request, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def platform_id():
    system = {"Darwin": "darwin", "Linux": "linux", "Windows": "windows"}[platform.system()]
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    return f"{system}-{machine}"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def unpack(zip_path, dest):
    if dest.exists():
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    if platform.system() == "Darwin":
        # ditto keeps symlinks, permissions and code signatures inside the PyInstaller bundle
        subprocess.check_call(["ditto", "-x", "-k", str(zip_path), str(dest.parent)])
    else:
        with zipfile.ZipFile(zip_path) as z:
            for info in z.infolist():
                z.extract(info, dest.parent)
                mode = info.external_attr >> 16
                if mode:
                    os.chmod(dest.parent / info.filename, mode & 0o7777)
    exe = dest / ("openkeyscan-analyzer.exe" if platform.system() == "Windows" else "openkeyscan-analyzer")
    if not exe.exists():
        sys.exit(f"error: {exe} missing after unpacking {zip_path}")
    print(f"sidecar ready: {exe}")


def verified_local_export(manifest, model, got):
    """Same rule as KeyDetectorOnnx: a model whose sha256 differs from the manifest is accepted only with a
    provenance record (written by ml/export_openkeyscan.py) that matches the file and records a passing
    parity check against the pinned upstream commit + checkpoint. Returns (ok, reason_or_platform)."""
    prov = model.with_name(model.name + ".provenance.json")
    if not prov.exists():
        return False, f"no {prov.name}"
    try:
        p = json.loads(prov.read_text())
    except ValueError:
        return False, f"{prov.name} is not valid JSON"
    if p.get("sha256") != got:
        return False, "provenance sha256 does not match the file"
    if p.get("source_commit") != manifest["openkeyscan_source"]["commit"]:
        return False, "exported from a different upstream commit"
    if p.get("checkpoint_sha256") != manifest["openkeyscan3_pt"]["sha256"]:
        return False, "exported from a different checkpoint"
    parity, threshold = p.get("parity_max_abs_logits", -1), p.get("parity_threshold", 0)
    if not (0 <= parity < threshold <= 1e-4) or not p.get("argmax_match"):
        return False, "provenance does not record a passing parity check"
    return True, p.get("platform", "?")


def ensure_onnx(manifest, build_dir, python):
    """Phase B model: verify build/models/openkeyscan3.onnx, downloading or exporting it if needed."""
    entry = manifest["openkeyscan3_onnx"]
    dest = build_dir / "models" / entry["file"]
    if dest.exists():
        got = sha256(dest)
        if got == entry["sha256"]:
            print(f"model ready: {dest}")
            return 0
        ok, info = verified_local_export(manifest, dest, got)
        if ok:
            print(f"model ready: {dest} (verified local export from {info}; sha256 {got[:12]}... differs from the manifest)")
            return 0
        if not python and not entry.get("url"):
            sys.exit(f"error: {dest} has sha256 {got[:12]}..., not the manifest's, and is not a verified local export ({info})")
    if entry.get("url"):
        dest.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(entry["url"], dest)
    elif python:
        fixtures = sorted(str(p) for p in (ROOT / "tests" / "fixtures").glob("*.wav"))
        if not fixtures:
            sys.exit("error: the parity check needs fixtures: cmake --build build --target fixtures")
        subprocess.check_call([python, str(ROOT / "ml" / "export_openkeyscan.py"), "--out", str(dest), *fixtures])
    else:
        print(f"No {entry['file']} yet (manifest url is null). Export it with a Python 3.12 env holding the pins\n"
              f"in models/manifest.json -> openkeyscan3_onnx.export.pins:\n"
              f"  python3 tools/models.py --python /path/to/venv/bin/python")
        return 1
    got = sha256(dest)
    if got != entry["sha256"]:
        ok, info = verified_local_export(manifest, dest, got)
        if not ok:
            sys.exit(f"error: sha256 mismatch for {dest}: {got} != {entry['sha256']} ({info})")
        print(f"model ready: {dest} (verified local export from {info}; sha256 {got[:12]}... differs from the manifest)")
        return 0
    print(f"model ready: {dest}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", default=str(ROOT / "build"))
    ap.add_argument("--build", action="store_true", help="build the sidecar from source if no artefact is available")
    ap.add_argument("--python", help="Python 3.12 env with the export pins: export the ONNX model if missing")
    ap.add_argument("--only", choices=["sidecar", "onnx"], help="handle just one artefact")
    args = ap.parse_args()

    manifest = json.loads((ROOT / "models" / "manifest.json").read_text())
    onnx_status = 0 if args.only == "sidecar" else ensure_onnx(manifest, Path(args.build_dir), args.python)
    if args.only == "onnx":
        return onnx_status
    entry = manifest["openkeyscan_sidecar"]
    plat = platform_id()
    art = entry["artifacts"].get(plat)
    if art is None:
        sys.exit(f"error: no sidecar artifact entry for {plat}")

    build_dir = Path(args.build_dir)
    dest = build_dir / "sidecar" / "openkeyscan-analyzer"
    local_zip = build_dir / "sidecar" / f"openkeyscan-analyzer-{plat}.zip"

    if art.get("url") and art.get("sha256"):
        local_zip.parent.mkdir(parents=True, exist_ok=True)
        if not local_zip.exists() or sha256(local_zip) != art["sha256"]:
            print(f"downloading {art['url']}")
            urllib.request.urlretrieve(art["url"], local_zip)
        got = sha256(local_zip)
        if got != art["sha256"]:
            sys.exit(f"error: sha256 mismatch for {local_zip}: {got} != {art['sha256']}")
        return unpack(local_zip, dest)

    if not local_zip.exists() and args.build:
        subprocess.check_call([str(ROOT / "ml" / "sidecar" / "build_openkeyscan_sidecar.sh"), str(build_dir)])

    if local_zip.exists():
        got = sha256(local_zip)
        if art.get("sha256") and got != art["sha256"]:
            sys.exit(f"error: sha256 mismatch for {local_zip}: {got} != {art['sha256']}")
        if not art.get("sha256"):
            print(f"note: manifest has no sha256 pinned for {plat}; this build is {got}")
        return unpack(local_zip, dest)

    print(f"No sidecar artefact for {plat} yet (manifest url/sha256 are null).\n"
          f"Build it from the pinned source:  ml/sidecar/build_openkeyscan_sidecar.sh {build_dir}\n"
          f"or:  python3 tools/models.py --build")
    return 1


if __name__ == "__main__":
    sys.exit(main() or 0)
