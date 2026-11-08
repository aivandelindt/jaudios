#!/usr/bin/env python3
"""Checks an `app-cli export` folder the way a DAW would read it (export-and-drag-drop acceptance).

A DAW places a dropped file's start on the bar line it is dropped on. So:
  - MIDI: type 1, a tempo, 4/4 and a key signature in the conductor track, named part tracks; the
    drum kicks sit on the 1/16 tick grid, nearly all of them on beats (fills aside).
  - WAV: every kick in the drums MIDI starts in the drums WAV at the same time, read through the
    MIDI's tempo map (within a few milliseconds), so audio and MIDI line up on the same bars.

Stdlib only. usage: tools/check_export_alignment.py EXPORT_DIR
"""
import json
import struct
import sys
import wave
from pathlib import Path


def read_vlq(data, i):
    value = 0
    while True:
        b = data[i]
        i += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, i


def parse_midi(path):
    data = Path(path).read_bytes()
    assert data[:4] == b"MThd", "not a MIDI file"
    fmt, ntracks, division = struct.unpack(">HHH", data[8:14])
    i, tracks = 14, []
    for _ in range(ntracks):
        assert data[i:i + 4] == b"MTrk"
        length = struct.unpack(">I", data[i + 4:i + 8])[0]
        j, end, tick, status, events = i + 8, i + 8 + length, 0, 0, []
        while j < end:
            delta, j = read_vlq(data, j)
            tick += delta
            b = data[j]
            if b == 0xFF:
                kind = data[j + 1]
                n, j = read_vlq(data, j + 2)
                events.append((tick, "meta", kind, data[j:j + n]))
                j += n
            elif b in (0xF0, 0xF7):
                n, j = read_vlq(data, j + 1)
                j += n
            else:
                if b & 0x80:
                    status, j = b, j + 1
                size = 1 if status >> 4 in (0xC, 0xD) else 2
                events.append((tick, "midi", status, data[j:j + size]))
                j += size
        tracks.append(events)
        i = end
    return fmt, division, tracks


def tick_to_seconds(tempo_map, ppq, tick):
    """tempo_map: sorted [(tick, microseconds per quarter)]."""
    seconds, last_tick, micros = 0.0, 0, 500000
    for t, m in tempo_map:
        if t >= tick:
            break
        seconds += (t - last_tick) * micros / 1e6 / ppq
        last_tick, micros = t, m
    return seconds + (tick - last_tick) * micros / 1e6 / ppq


def read_wav_mono(path):
    with wave.open(str(path), "rb") as w:
        rate, width, channels, frames = w.getframerate(), w.getsampwidth(), w.getnchannels(), w.getnframes()
        raw = w.readframes(frames)
    assert width == 3, "expected 24-bit PCM"
    samples = []
    for k in range(0, len(raw), 3 * channels):
        v = int.from_bytes(raw[k:k + 3], "little", signed=True) / 8388608.0
        samples.append(v)
    return rate, samples


def main():
    folder = Path(sys.argv[1])
    project = json.loads(next(folder.glob("*.jaudios.json")).read_text())
    assert project["version"] == 2, "project version"
    drums_mid = next(folder.glob("* - Drums (*).mid"))
    drums_wav = next(folder.glob("* - Drums (*).wav"))
    fmt, ppq, tracks = parse_midi(drums_mid)
    assert fmt == 1 and len(tracks) == 2, f"type 1 with conductor + 1 part (got type {fmt}, {len(tracks)} tracks)"
    conductor = tracks[0]
    tempo_map = [(t, int.from_bytes(d, "big")) for t, k, kind, d in conductor if k == "meta" and kind == 0x51]
    time_sig = [d for t, k, kind, d in conductor if k == "meta" and kind == 0x58]
    key_sig = [d for t, k, kind, d in conductor if k == "meta" and kind == 0x59]
    names = [d.decode() for t, k, kind, d in tracks[1] if k == "meta" and kind == 0x03]
    assert tempo_map and tempo_map[0][0] == 0, "tempo at tick 0"
    assert time_sig and time_sig[0][:2] == bytes([4, 2]), "4/4"
    assert key_sig, "key signature"
    assert names == ["Drums"], f"track name {names}"
    bpm = 60e6 / tempo_map[0][1]
    assert abs(bpm - project["analysis"]["bpm"]) < 0.5, f"tempo {bpm:.2f} vs analysis {project['analysis']['bpm']:.2f}"

    kicks = [t for t, k, st, d in tracks[1] if k == "midi" and st >> 4 == 9 and d[1] > 0 and d[0] == 36]
    assert kicks, "no kicks"
    off_grid = [t for t in kicks if t % (ppq // 4)]
    assert not off_grid, f"kicks off the 1/16 grid: {off_grid[:5]}"
    on_beat = sum(1 for t in kicks if t % ppq == 0)
    assert on_beat >= 0.9 * len(kicks), f"only {on_beat} of {len(kicks)} kicks on beats (fills excepted)"
    downbeats = project["analysis"]["downbeats"]

    rate, audio = read_wav_mono(drums_wav)
    worst = 0.0
    for t in kicks[:64]:
        expected = tick_to_seconds(tempo_map, ppq, t)
        at = int(expected * rate)
        # the level just before (hats and tails ring there), then the first sample well above it
        before = max((abs(v) for v in audio[max(0, at - int(0.025 * rate)):max(0, at - int(0.005 * rate))]), default=0.0)
        threshold = max(0.05, 3.0 * before)
        lo = max(0, at - int(0.005 * rate))
        window = audio[lo:at + int(0.02 * rate)]
        onset = next((n for n, v in enumerate(window) if abs(v) > threshold), None)
        assert onset is not None, f"no kick in the WAV near {expected:.3f} s"
        worst = max(worst, abs((lo + onset) / rate - expected))
    assert worst < 0.003, f"WAV kick onsets are {worst * 1000:.1f} ms off the MIDI grid"
    first_bar_tick = kicks[0] - kicks[0] % (4 * ppq)
    print(json.dumps({
        "bpm": round(bpm, 3),
        "ppq": ppq,
        "tempo_events": len(tempo_map),
        "key_signature": list(key_sig[0]),
        "kicks": len(kicks),
        "kicks_on_beats": on_beat,
        "first_kick_tick": kicks[0],
        "first_bar_tick": first_bar_tick,
        "first_downbeat_sec": downbeats[0] if downbeats else None,
        "worst_wav_onset_ms": round(worst * 1000, 3),
        "result": "PASS",
    }, indent=2))


if __name__ == "__main__":
    main()
