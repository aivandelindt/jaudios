#!/usr/bin/env python3
"""Export openkeyscan3.pt (pinned in models/manifest.json) to ONNX and verify parity.

    python ml/export_openkeyscan.py [--out models/openkeyscan3.onnx] FIXTURE.wav [FIXTURE.wav ...]

- model built by upstream eval.load_model() (KeyNet, Nf=64, p=0.3), eval(), CPU
- input "cqt" (1, 1, 105, time), output "logits" (1, 24); dynamic time axis, opset 17
- onnxsim simplification (dynamic time axis kept); the simplified model is the shipped one
- parity: PyTorch vs ONNX Runtime (CPU) on each fixture's reference CQT, max |dlogits| < 1e-4
- prints sha256 + size for models/manifest.json
"""
import argparse, hashlib, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import openkeyscan_source  # noqa: E402

import numpy as np  # noqa: E402
import torch  # noqa: E402
import onnx  # noqa: E402
import onnxruntime as ort  # noqa: E402
import onnxsim  # noqa: E402
from eval import load_model  # noqa: E402  (upstream)
from tests.cqt_reference import reference  # noqa: E402

OPSET = 17
PARITY = 1e-4


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(openkeyscan_source.ROOT / "models" / "openkeyscan3.onnx"))
    ap.add_argument("fixtures", nargs="+")
    args = ap.parse_args()

    _, ckpt = openkeyscan_source.ensure_checkout()
    torch.manual_seed(0)
    model = load_model(str(ckpt), torch.device("cpu"))  # upstream: eval() already applied
    model.eval()

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.randn(1, 1, 105, 100)
    torch.onnx.export(model, dummy, str(out), opset_version=OPSET, do_constant_folding=True,
                      input_names=["cqt"], output_names=["logits"],
                      dynamic_axes={"cqt": {3: "time"}, "logits": {}})

    simplified, ok = onnxsim.simplify(onnx.load(str(out)))
    if not ok:
        sys.exit("onnxsim could not validate the simplified model")
    onnx.checker.check_model(simplified)
    onnx.save(simplified, str(out))
    print(f"exported + simplified: opset {OPSET}, onnx {onnx.__version__}, onnxsim {onnxsim.__version__}, "
          f"torch {torch.__version__}, nodes={len(simplified.graph.node)}")

    sess = ort.InferenceSession(str(out), providers=["CPUExecutionProvider"])
    worst = 0.0
    for f in args.fixtures:
        spec = reference(f)[None, None]  # (1, 1, 105, T)
        with torch.no_grad():
            ref = model(torch.from_numpy(spec)).numpy()
        got = sess.run(["logits"], {"cqt": spec})[0]
        diff = float(np.max(np.abs(ref - got)))
        worst = max(worst, diff)
        print(f"{Path(f).name:24s} T={spec.shape[-1]:4d} argmax torch={ref.argmax()} ort={got.argmax()} max|d|={diff:.2e}")
        if ref.argmax() != got.argmax():
            sys.exit("argmax mismatch")

    print(f"worst max|dlogits| = {worst:.2e} (threshold {PARITY:g})")
    if worst >= PARITY:
        sys.exit("parity FAILED")

    data = out.read_bytes()
    print(f"{out}  size={len(data)}  sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
