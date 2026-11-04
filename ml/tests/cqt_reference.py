#!/usr/bin/env python3
"""Reference model inputs for the C++ CQT golden test and the ONNX parity test.

Calls upstream's own preprocess_audio() (openkeyscan_analyzer_server.py @ pinned commit): librosa
CQT -> abs -> log1p -> [:, 0:-2]. Never a re-implementation.

    python ml/tests/cqt_reference.py OUT_DIR AUDIO [AUDIO ...]

Writes OUT_DIR/<stem>.cqt.npy (float32, shape (105, T)) and OUT_DIR/<stem>.logits.npy (float32,
shape (24,)): PyTorch logits of upstream's model on that input (what the Phase A server argmaxes).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import openkeyscan_source  # noqa: E402,F401  (pinned checkout on sys.path)
import numpy as np  # noqa: E402
import openkeyscan_analyzer_server as server  # noqa: E402  (main() is guarded; importing does not start it)


def reference(audio_path):
    # C order: librosa returns a Fortran-ordered CQT and np.save would keep that layout
    return np.ascontiguousarray(server.preprocess_audio(Path(audio_path)).numpy()[0], dtype=np.float32)  # (105, T)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    import torch
    from eval import load_model  # upstream
    _, ckpt = openkeyscan_source.ensure_checkout()
    model = load_model(str(ckpt), torch.device("cpu"))

    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    for a in sys.argv[2:]:
        spec = reference(a)
        dest = out / (Path(a).stem + ".cqt.npy")
        np.save(dest, spec)
        with torch.no_grad():
            logits = model(torch.from_numpy(spec)[None, None]).numpy()[0].astype(np.float32)
        np.save(out / (Path(a).stem + ".logits.npy"), logits)
        print(f"{dest} {spec.shape} argmax={int(logits.argmax())}")


if __name__ == "__main__":
    main()
