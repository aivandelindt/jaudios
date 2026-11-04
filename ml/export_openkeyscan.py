#!/usr/bin/env python3
"""Export openkeyscan3.pt (pinned in models/manifest.json) to ONNX and verify parity.

    python ml/export_openkeyscan.py [--out models/openkeyscan3.onnx] FIXTURE.wav [FIXTURE.wav ...]

- model built by upstream eval.load_model() (KeyNet, Nf=64, p=0.3), eval(), CPU
- input "cqt" (1, 1, 105, time), output "logits" (1, 24); dynamic time axis, opset 17
- onnxsim simplification (dynamic time axis kept); the simplified model is the shipped one
- parity: PyTorch vs ONNX Runtime (CPU) on each fixture's reference CQT, max |dlogits| < 1e-4
- prints sha256 + size for models/manifest.json
- writes <out>.provenance.json (sha256, parity, source commit, checkpoint, platform, versions). A model
  whose sha256 differs from the manifest (e.g. exported on macOS arm64) is accepted by the app and by
  tools/models.py only with a provenance record that matches the file and records a passing parity.

The three stages run in separate processes, each loading only the native libraries it needs:
  1. torch      : export the raw graph + PyTorch reference logits on the fixtures (-> .npz)
  2. onnxsim    : simplify + check the graph (onnx / onnxsim, no torch)
  3. onnxruntime: parity of the simplified model against the saved logits (no torch)
Loading torch together with the onnx / onnxsim / onnxruntime wheels in one process segfaulted on
macOS arm64 (each wheel bundles its own native protobuf / onnx code); on Linux it happened to work.
The orchestrating process imports none of them.
"""
import argparse, datetime, hashlib, json, platform, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import openkeyscan_source  # noqa: E402  (paths + pinned checkout; imports nothing heavy)

OPSET = 17
PARITY = 1e-4


def stage_torch(raw: Path, refs: Path, fixtures):
    import onnx  # noqa: F401  load before torch: torch.onnx.export imports it lazily
    import numpy as np
    import torch
    from eval import load_model  # upstream
    from tests.cqt_reference import reference

    _, ckpt = openkeyscan_source.ensure_checkout()
    torch.manual_seed(0)
    model = load_model(str(ckpt), torch.device("cpu"))  # upstream: eval() already applied
    model.eval()

    dummy = torch.randn(1, 1, 105, 100)
    torch.onnx.export(model, dummy, str(raw), opset_version=OPSET, do_constant_folding=True,
                      input_names=["cqt"], output_names=["logits"],
                      dynamic_axes={"cqt": {3: "time"}, "logits": {}})

    arrays = {}
    for i, f in enumerate(fixtures):
        spec = reference(f)[None, None]  # (1, 1, 105, T)
        with torch.no_grad():
            arrays[f"spec{i}"] = spec
            arrays[f"logits{i}"] = model(torch.from_numpy(spec)).numpy()
    np.savez(refs, **arrays)
    print(f"[torch] exported raw graph, torch {torch.__version__}, {len(fixtures)} reference(s)", flush=True)


def stage_simplify(raw: Path, out: Path):
    import onnx
    import onnxsim

    simplified, ok = onnxsim.simplify(onnx.load(str(raw)))
    if not ok:
        sys.exit("onnxsim could not validate the simplified model")
    onnx.checker.check_model(simplified)
    onnx.save(simplified, str(out))
    print(f"[onnxsim] simplified: opset {OPSET}, onnx {onnx.__version__}, onnxsim {onnxsim.__version__}, "
          f"nodes={len(simplified.graph.node)}", flush=True)


def stage_parity(out: Path, refs: Path, result: Path, fixtures):
    import numpy as np
    import onnxruntime as ort

    data = np.load(refs)
    sess = ort.InferenceSession(str(out), providers=["CPUExecutionProvider"])
    worst = 0.0
    for i, f in enumerate(fixtures):
        spec, ref = data[f"spec{i}"], data[f"logits{i}"]
        got = sess.run(["logits"], {"cqt": spec})[0]
        diff = float(np.max(np.abs(ref - got)))
        worst = max(worst, diff)
        print(f"{Path(f).name:24s} T={spec.shape[-1]:4d} argmax torch={ref.argmax()} ort={got.argmax()} max|d|={diff:.2e}")
        if ref.argmax() != got.argmax():
            sys.exit("argmax mismatch")
    print(f"[onnxruntime {ort.__version__}] worst max|dlogits| = {worst:.2e} (threshold {PARITY:g})")
    if worst >= PARITY:
        sys.exit("parity FAILED")
    result.write_text(json.dumps({"worst": worst, "fixtures": len(fixtures), "onnxruntime": ort.__version__}))


def run_stage(*args):
    cmd = [sys.executable, str(Path(__file__).resolve()), "--stage", *map(str, args)]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"stage {args[0]} failed (exit {result.returncode})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(openkeyscan_source.ROOT / "models" / "openkeyscan3.onnx"))
    ap.add_argument("--stage", nargs="+", help=argparse.SUPPRESS)  # internal
    ap.add_argument("fixtures", nargs="*")
    args = ap.parse_args()

    if args.stage:
        name, *rest = args.stage
        if name == "torch":
            return stage_torch(Path(rest[0]), Path(rest[1]), rest[2:])
        if name == "simplify":
            return stage_simplify(Path(rest[0]), Path(rest[1]))
        if name == "parity":
            return stage_parity(Path(rest[0]), Path(rest[1]), Path(rest[2]), rest[3:])
        sys.exit(f"unknown stage {name}")

    if not args.fixtures:
        ap.error("at least one fixture is required for the parity check")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    openkeyscan_source.ensure_checkout()

    provenance = out.with_name(out.name + ".provenance.json")
    provenance.unlink(missing_ok=True)  # never leave a record from an earlier export next to a new file

    with tempfile.TemporaryDirectory() as tmp:
        raw, refs, parity = Path(tmp) / "raw.onnx", Path(tmp) / "refs.npz", Path(tmp) / "parity.json"
        run_stage("torch", raw, refs, *args.fixtures)
        run_stage("simplify", raw, out)
        run_stage("parity", out, refs, parity, *args.fixtures)
        checked = json.loads(parity.read_text())

    data = out.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    pins = openkeyscan_source.MANIFEST["openkeyscan3_onnx"]["export"]["pins"]
    provenance.write_text(json.dumps({
        "file": out.name,
        "sha256": sha,
        "size": len(data),
        "matches_manifest": sha == openkeyscan_source.MANIFEST["openkeyscan3_onnx"]["sha256"],
        "parity_max_abs_logits": checked["worst"],
        "parity_threshold": PARITY,
        "argmax_match": True,
        "parity_fixtures": checked["fixtures"],
        "source_commit": openkeyscan_source.SOURCE["commit"],
        "checkpoint_sha256": openkeyscan_source.CHECKPOINT["sha256"],
        "platform": f"{sys.platform}-{platform.machine()}",
        "python": platform.python_version(),
        "pins": pins,
        "exported_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
    }, indent=2) + "\n")
    print(f"{out}  size={len(data)}  sha256={sha}")
    print(f"{provenance}")


if __name__ == "__main__":
    main()
