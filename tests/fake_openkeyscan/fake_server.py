#!/usr/bin/env python3
"""Fake openkeyscan-analyzer for C++ client tests. Stdlib only.

Speaks the real NDJSON protocol (see engine/analysis/openkeyscan/PREPROCESSING.md):
  stdout: {"type":"ready"}, {"type":"heartbeat"}, responses; stderr: logs.

Behaviour via environment:
  FAKE_MODE      normal | never_ready | crash_on_request | hang_on_request | error | noisy
                 crash_once (crash on the first request of the first process, then normal)
  FAKE_CLASS     class_id to answer (default 7 = 8A, A minor)
  FAKE_STATE     file used to count process starts (for restart tests)
"""
import json, os, sys, time

mode = os.environ.get("FAKE_MODE", "normal")
cls = int(os.environ.get("FAKE_CLASS", "7"))
state = os.environ.get("FAKE_STATE")

starts = 0
if state:
    try:
        starts = int(open(state).read() or "0")
    except (FileNotFoundError, ValueError):
        starts = 0
    starts += 1
    open(state, "w").write(str(starts))

print(f"fake server start #{starts} mode={mode} argv={sys.argv[1:]}", file=sys.stderr, flush=True)

def send(obj):
    print(json.dumps(obj), flush=True)

def camelot(c):
    return f"{c % 12 + 1}{'A' if c < 12 else 'B'}"

def openkey(c):
    n = int(camelot(c)[:-1])
    return f"{(n - 8) % 12 + 1}{'m' if c < 12 else 'd'}"

if mode == "never_ready":
    time.sleep(3600)

if mode == "noisy":
    print("some library printed to stdout", flush=True)   # non-JSON line must be ignored
    send({"type": "heartbeat"})

send({"type": "ready"})

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    req = json.loads(line)
    print(f"request {req}", file=sys.stderr, flush=True)

    if mode == "crash_on_request" or (mode == "crash_once" and starts == 1):
        sys.exit(3)
    if mode == "hang_on_request":
        time.sleep(3600)
    if mode == "noisy":
        send({"type": "heartbeat"})
        send({"id": "someone-else", "status": "success", "camelot": "1A", "openkey": "6m",
              "key": "G# minor/Ab minor", "class_id": 0, "filename": "x.wav", "generation": 0})

    path = req.get("path", "")
    name = os.path.basename(path)
    if not os.path.isabs(path) or not os.path.exists(path):
        send({"id": req["id"], "status": "error", "error": "File not found", "filename": name, "generation": 0})
    elif mode == "error":
        send({"id": req["id"], "status": "error", "error": "decoder exploded", "filename": name, "generation": 0})
    else:
        send({"id": req["id"], "status": "success", "camelot": camelot(cls), "openkey": openkey(cls),
              "key": "?", "class_id": cls, "filename": name, "generation": 0})
