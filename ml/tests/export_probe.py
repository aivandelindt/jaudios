#!/usr/bin/env python3
"""Pinpoints which library combination crashes during the ONNX export (diagnostic, CI only).

    python ml/tests/export_probe.py RAW_ONNX SIMPLIFIED_ONNX

Each case runs in a fresh process with faulthandler on and reports ok / exit code / signal, so one
CI run shows whether a crash comes from a single wheel or from two wheels loaded together.
"""
import signal, subprocess, sys

CASES = {
    "torch only (import)":             "import torch",
    "onnx only (load raw)":            "import onnx; onnx.load(RAW)",
    "onnxsim only (simplify raw)":     "import onnx, onnxsim; m, ok = onnxsim.simplify(onnx.load(RAW)); assert ok",
    "onnxruntime only (session)":      "import onnxruntime as ort; ort.InferenceSession(SIMPLIFIED, providers=['CPUExecutionProvider'])",
    "torch then onnx (load raw)":      "import torch, onnx; onnx.load(RAW)",
    "onnx then torch (load raw)":      "import onnx, torch; onnx.load(RAW)",
    "torch then onnxsim (simplify)":   "import torch, onnx, onnxsim; m, ok = onnxsim.simplify(onnx.load(RAW)); assert ok",
    "torch then onnxruntime (session)": "import torch, onnxruntime as ort; ort.InferenceSession(SIMPLIFIED, providers=['CPUExecutionProvider'])",
    "onnxsim then onnxruntime":        "import onnx, onnxsim, onnxruntime as ort; onnxsim.simplify(onnx.load(RAW)); ort.InferenceSession(SIMPLIFIED, providers=['CPUExecutionProvider'])",
}


def main():
    raw, simplified = sys.argv[1], sys.argv[2]
    width = max(map(len, CASES))
    for name, code in CASES.items():
        prog = f"RAW, SIMPLIFIED = {raw!r}, {simplified!r}\n{code}"
        r = subprocess.run([sys.executable, "-X", "faulthandler", "-c", prog], capture_output=True, text=True)
        if r.returncode == 0:
            status = "ok"
        elif r.returncode < 0:
            status = f"CRASH {signal.Signals(-r.returncode).name}"
        else:
            status = f"exit {r.returncode}"
        print(f"{name:{width}s}  {status}", flush=True)
        if r.returncode != 0:
            tail = (r.stderr or "").strip().splitlines()[-25:]
            print("    " + "\n    ".join(tail), flush=True)


if __name__ == "__main__":
    main()
