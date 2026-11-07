#!/usr/bin/env python3
"""Export HT-Demucs (htdemucs, MIT) to ONNX and verify parity against upstream PyTorch.

    python ml/export_demucs.py [--out build/models/htdemucs.onnx] FIXTURE.wav [FIXTURE.wav ...]

What is exported: the network core of upstream `HTDemucs.forward`, without its complex-valued
STFT / ISTFT (ONNX cannot represent complex tensors). The wrapper below does not re-implement the
architecture: it temporarily replaces the four complex helpers on the upstream model and runs
upstream's own forward():
    _spec / _magnitude -> return the precomputed complex-as-channels (CaC) spectrogram input
    _mask              -> pass the real spectral output through unchanged
    _ispec             -> capture that spectral output, return 0 (forward then returns the time branch)
Graph (static shapes, one 7.8 s segment, opset 17):
    mix  float32 (1, 2, 343980)        normalised stereo mix
    spec float32 (1, 4, 2048, 336)     CaC spectrogram of `mix` (HTDemucs._spec + _magnitude)
 -> spec_out float32 (1, 4, 4, 2048, 336)   per-source CaC spectrogram (before _mask / _ispec)
    wave_out float32 (1, 4, 2, 343980)      per-source time-branch waveform
    sources = model.sources = drums, bass, other, vocals; output = istft(spec_out) + wave_out.
The C++ StemSeparator does the STFT / ISTFT and demucs.apply_model's segmenting.

Parity (stage 2, onnxruntime + torch): for one segment of each fixture, upstream forward(mix) vs
istft(ORT spec_out) + ORT wave_out, max |d| < 1e-3 (ml-model-pipeline threshold for htdemucs).
Writes <out>.provenance.json (sha256, parity, demucs version, checkpoint sha256, platform, pins).
"""
import argparse, datetime, hashlib, json, platform, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = json.loads((ROOT / "models" / "manifest.json").read_text())
ENTRY = MANIFEST["htdemucs_onnx"]
CHECKPOINT = MANIFEST["htdemucs_pt"]
PARITY = 1e-3
SEGMENT = 343980  # int(39/5 * 44100): HTDemucs training segment


def load_model():
    import torch
    import demucs
    from demucs.pretrained import get_model

    if demucs.__version__ != CHECKPOINT["demucs_version"]:
        sys.exit(f"demucs {demucs.__version__} installed, manifest pins {CHECKPOINT['demucs_version']}")
    bag = get_model(CHECKPOINT["name"])
    model = bag.models[0].eval()
    ckpt = Path(torch.hub.get_dir()) / "checkpoints" / CHECKPOINT["file"]
    digest = hashlib.sha256(ckpt.read_bytes()).hexdigest()
    if digest != CHECKPOINT["sha256"]:
        sys.exit(f"checkpoint sha256 mismatch: {digest} != {CHECKPOINT['sha256']}")
    return model


class Core:
    """torch.nn.Module wrapper around an upstream HTDemucs, built lazily (torch imported per stage)."""

    @staticmethod
    def make(model):
        import torch

        class _Core(torch.nn.Module):
            def __init__(self, m):
                super().__init__()
                self.m = m

            def forward(self, mix, spec):
                m, store = self.m, {}
                saved = (m._spec, m._magnitude, m._mask, m._ispec)

                def ispec(zout, length=None, scale=0):
                    store["spec_out"] = zout
                    return torch.zeros((), dtype=mix.dtype)

                m._spec = lambda x: None
                m._magnitude = lambda z: spec
                m._mask = lambda z, x: x
                m._ispec = ispec
                try:
                    wave_out = m.forward(mix)  # = time branch + 0
                finally:
                    m._spec, m._magnitude, m._mask, m._ispec = saved
                return store["spec_out"], wave_out

        return _Core(model).eval()


def segment_of(path):
    """First full 7.8 s segment of a fixture, normalised like demucs.api (mono reference)."""
    import numpy as np
    import soundfile as sf

    wav, sr = sf.read(path, dtype="float32", always_2d=True)
    if sr != 44100:
        sys.exit(f"{path}: {sr} Hz (fixtures are 44.1 kHz)")
    wav = wav.T
    if wav.shape[0] == 1:
        wav = np.repeat(wav, 2, axis=0)
    seg = np.zeros((2, SEGMENT), dtype=np.float32)
    n = min(SEGMENT, wav.shape[1])
    seg[:, :n] = wav[:2, :n]
    ref = seg.mean(0)
    return ((seg - ref.mean()) / (ref.std(ddof=1) + 1e-8)).astype(np.float32)


def stage_export(out: Path):
    import onnx  # noqa: F401  load before torch (torch.onnx.export imports it)
    import torch

    model = load_model()
    core = Core.make(model)
    torch.manual_seed(0)
    mix = torch.randn(1, 2, SEGMENT)
    with torch.no_grad():
        spec = model._magnitude(model._spec(mix))
    # Traced with grad enabled on purpose: in eval mode under no_grad, nn.MultiheadAttention and
    # nn.TransformerEncoderLayer take their fused fast path (aten::_native_multi_head_attention),
    # which has no ONNX symbolic in torch 2.2 (torch.backends.mha.set_fastpath_enabled is 2.3+).
    # With grad enabled and weights requiring grad they run the regular, exportable path.
    assert torch.is_grad_enabled() and all(p.requires_grad for p in model.parameters())
    torch.onnx.export(core, (mix, spec), str(out), opset_version=17, do_constant_folding=True,
                      input_names=["mix", "spec"], output_names=["spec_out", "wave_out"])
    print(f"[torch {torch.__version__}] exported htdemucs core: spec {tuple(spec.shape)}", flush=True)


def stage_parity(out: Path, result: Path, fixtures):
    import numpy as np
    import onnxruntime as ort
    import torch

    model = load_model()
    sess = ort.InferenceSession(str(out), providers=["CPUExecutionProvider"])
    worst = 0.0
    for f in fixtures:
        mix = torch.from_numpy(segment_of(f))[None]
        with torch.no_grad():
            ref = model(mix)  # upstream forward, (1, 4, 2, SEGMENT)
            spec = model._magnitude(model._spec(mix))
            spec_out, wave_out = sess.run(None, {"mix": mix.numpy(), "spec": spec.numpy()})
            z = model._mask(None, torch.from_numpy(spec_out))
            got = model._ispec(z, SEGMENT) + torch.from_numpy(wave_out)
        diff = float((ref - got).abs().max())
        snr = float(10 * torch.log10((ref ** 2).sum() / ((ref - got) ** 2).sum().clamp_min(1e-30)))
        worst = max(worst, diff)
        print(f"{Path(f).name:24s} max|d|={diff:.2e}  SNR={snr:.1f} dB", flush=True)
    print(f"[onnxruntime {ort.__version__}] worst max|d| = {worst:.2e} (threshold {PARITY:g})", flush=True)
    if worst >= PARITY:
        sys.exit("parity FAILED")
    result.write_text(json.dumps({"worst": worst, "fixtures": len(fixtures), "onnxruntime": ort.__version__}))


def run_stage(*args):
    r = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--stage", *map(str, args)])
    if r.returncode != 0:
        sys.exit(f"stage {args[0]} failed (exit {r.returncode})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "build" / "models" / ENTRY["file"]))
    ap.add_argument("--stage", nargs="+", help=argparse.SUPPRESS)
    ap.add_argument("fixtures", nargs="*")
    args = ap.parse_args()

    if args.stage:
        name, *rest = args.stage
        if name == "export":
            return stage_export(Path(rest[0]))
        if name == "parity":
            return stage_parity(Path(rest[0]), Path(rest[1]), rest[2:])
        sys.exit(f"unknown stage {name}")
    if not args.fixtures:
        ap.error("at least one fixture is required for the parity check")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    provenance = out.with_name(out.name + ".provenance.json")
    provenance.unlink(missing_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        parity = Path(tmp) / "parity.json"
        run_stage("export", out)
        run_stage("parity", out, parity, *args.fixtures)
        checked = json.loads(parity.read_text())

    data = out.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    provenance.write_text(json.dumps({
        "file": out.name,
        "sha256": sha,
        "size": len(data),
        "matches_manifest": sha == ENTRY.get("sha256"),
        "parity_max_abs": checked["worst"],
        "parity_threshold": PARITY,
        "parity_fixtures": checked["fixtures"],
        "demucs_version": CHECKPOINT["demucs_version"],
        "checkpoint_sha256": CHECKPOINT["sha256"],
        "platform": f"{sys.platform}-{platform.machine()}",
        "python": platform.python_version(),
        "pins": ENTRY["export"]["pins"],
        "exported_at": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
    }, indent=2) + "\n")
    print(f"{out}  size={len(data)}  sha256={sha}")
    print(f"{provenance}")


if __name__ == "__main__":
    main()
