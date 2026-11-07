#!/usr/bin/env python3
"""Upstream Basic Pitch references for the C++ AudioToMidi golden tests.

Calls upstream basic-pitch 0.4.0 code only (inference.run_inference with its ONNX backend and
note_creation.model_output_to_notes / output_to_notes_polyphonic / get_pitch_bends), never a
re-implementation:

    python ml/tests/basic_pitch_reference.py MODEL.onnx OUT_DIR NAME [NAME ...]

For each tests/fixtures/<NAME>.wav writes (float32, C order, <= 2-D for tests/NpyReader.h):
  <NAME>.bp_audio.npy    (N,)       upstream's model input audio: librosa.load(sr=22050, mono=True)
  <NAME>.bp_note.npy     (T, 88)    unwrapped posteriors from run_inference
  <NAME>.bp_onset.npy    (T, 88)
  <NAME>.bp_contour.npy  (T, 264)
  <NAME>.bp_notes.json   note events for each parameter set in PARAMS (frame indices, pitch,
                         amplitude, times in seconds, pitch bends in 1/3-semitone units)
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent

# "skill": audio-to-midi skill defaults (frame 0.5, onset 0.5, min 58 ms). "upstream": predict() defaults.
PARAMS = {
    "skill": {"onset_thresh": 0.5, "frame_thresh": 0.5, "min_note_ms": 58.0},
    "upstream": {"onset_thresh": 0.5, "frame_thresh": 0.3, "min_note_ms": 127.70},
}


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    import numpy as np
    import librosa
    from basic_pitch import inference, note_creation as nc
    from basic_pitch.constants import AUDIO_SAMPLE_RATE, FFT_HOP

    model = inference.Model(sys.argv[1])
    assert model.model_type == inference.Model.MODEL_TYPES.ONNX
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)

    for name in sys.argv[3:]:
        wav = ROOT / "tests" / "fixtures" / f"{name}.wav"
        audio, _ = librosa.load(str(wav), sr=AUDIO_SAMPLE_RATE, mono=True)  # what get_audio_input() does
        output = inference.run_inference(wav, model)
        np.save(out / f"{name}.bp_audio.npy", np.ascontiguousarray(audio, dtype=np.float32))
        for k in ("note", "onset", "contour"):
            np.save(out / f"{name}.bp_{k}.npy", np.ascontiguousarray(output[k], dtype=np.float32))

        result = {}
        for label, p in PARAMS.items():
            min_len = int(np.round(p["min_note_ms"] / 1000 * (AUDIO_SAMPLE_RATE / FFT_HOP)))  # as predict()
            frames = nc.output_to_notes_polyphonic(
                output["note"].copy(), output["onset"].copy(), onset_thresh=p["onset_thresh"],
                frame_thresh=p["frame_thresh"], min_note_len=min_len, infer_onsets=True,
                max_freq=None, min_freq=None, melodia_trick=True)
            with_bends = nc.get_pitch_bends(output["contour"], frames)
            times = nc.model_frames_to_time(output["contour"].shape[0])
            # model_output_to_notes() on fresh copies, to prove the frame-level list above is its input
            _, events = nc.model_output_to_notes(
                {k: v.copy() for k, v in output.items()}, onset_thresh=p["onset_thresh"],
                frame_thresh=p["frame_thresh"], min_note_len=min_len, melodia_trick=True)
            assert len(events) == len(with_bends)
            result[label] = {
                **p, "min_note_len_frames": min_len,
                "notes": [{"start": int(s), "end": int(e), "pitch": int(m), "amplitude": float(a),
                           "start_s": float(times[s]), "end_s": float(times[e]),
                           "bends": [int(b) for b in bends]}
                          for s, e, m, a, bends in with_bends],
            }
        (out / f"{name}.bp_notes.json").write_text(json.dumps(result))
        print(f"{name}: {len(audio)} samples @ 22.05 kHz, {output['note'].shape[0]} frames, "
              + ", ".join(f"{k} {len(v['notes'])} notes" for k, v in result.items()))


if __name__ == "__main__":
    main()
