#!/usr/bin/env python3
"""Upstream HT-Demucs references for the C++ StemSeparator golden tests.

Calls upstream demucs 4.1.0 code only (HTDemucs._spec / _magnitude / _mask / _ispec and
demucs.apply.apply_model), never a re-implementation:

    python ml/tests/demucs_reference.py OUT_DIR

Writes (float32, C order, 2-D so tests/NpyReader.h can read them):
  demucs_spec.npy      (4*2048, 336)  CaC spectrogram of the first normalised 7.8 s segment of
                                      loop_8bar_Gmaj.wav (HTDemucs._magnitude(_spec(x)))
  demucs_ispec_in.npy  (4*2048, 336)  seeded random CaC spectrogram (one source)
  demucs_ispec_out.npy (2, 343980)    HTDemucs._ispec(_mask(None, in), 343980)
  demucs_loop.npy      (4*2, N)       separation of the whole loop_8bar_Gmaj.wav (16.5 s: 3 segments,
                                      short last chunk), like demucs.api.Separator but shifts=0:
                                      normalise by the mono reference, apply_model(split=True,
                                      overlap=0.25, shifts=0), denormalise. Rows: source-major
                                      (drums, bass, other, vocals) x (left, right).
  demucs_short.npy     (4*2, 132300)  same for the first 3 s of techno_132_Fmin.wav (one padded segment)
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "ml"))
import export_demucs  # noqa: E402  (manifest pins + checkpoint check; imports nothing heavy)

SEG = export_demucs.SEGMENT


def load(name, seconds=None):
    import numpy as np
    import soundfile as sf

    wav, sr = sf.read(ROOT / "tests" / "fixtures" / f"{name}.wav", dtype="float32", always_2d=True)
    assert sr == 44100 and wav.shape[1] == 2, (sr, wav.shape)
    wav = np.ascontiguousarray(wav.T)
    return wav[:, : int(seconds * sr)] if seconds else wav


def separate(bag, wav):
    """demucs.api.Separator.separate_tensor with shifts=0 (deterministic)."""
    import torch
    from demucs.apply import apply_model

    wav = torch.from_numpy(wav)
    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    with torch.no_grad():
        out = apply_model(bag, ((wav - mean) / std)[None], shifts=0, split=True, overlap=0.25, progress=False)[0]
    out = out * std + mean
    return out.reshape(-1, out.shape[-1]).numpy()  # (4*2, N)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    import numpy as np
    import torch
    from demucs.pretrained import get_model

    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    model = export_demucs.load_model()  # checks demucs version + checkpoint sha256
    bag = get_model(export_demucs.CHECKPOINT["name"])
    assert list(model.sources) == ["drums", "bass", "other", "vocals"], model.sources

    def save(name, a):
        np.save(out / name, np.ascontiguousarray(a, dtype=np.float32))

    loop = load("loop_8bar_Gmaj")
    seg = loop[:, :SEG]
    ref = seg.mean(0)
    seg = (seg - ref.mean()) / (ref.std(ddof=1) + 1e-8)
    with torch.no_grad():
        spec = model._magnitude(model._spec(torch.from_numpy(seg)[None]))[0]  # (4, 2048, 336)
        save("demucs_spec.npy", spec.reshape(-1, spec.shape[-1]).numpy())

        g = torch.Generator().manual_seed(1234)
        rand = torch.randn((1, 1, 4, 2048, 336), generator=g)
        wave = model._ispec(model._mask(None, rand), SEG)[0, 0]  # (2, SEG)
        save("demucs_ispec_in.npy", rand.reshape(-1, 336).numpy())
        save("demucs_ispec_out.npy", wave.numpy())

    save("demucs_loop.npy", separate(bag, loop))
    save("demucs_short.npy", separate(bag, load("techno_132_Fmin", 3.0)))
    print(f"demucs references written to {out}")


if __name__ == "__main__":
    main()
