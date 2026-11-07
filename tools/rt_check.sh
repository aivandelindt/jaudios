#!/usr/bin/env bash
# Realtime-safety check (juce-standalone-scaffold acceptance).
#
# Builds an instrumented variant (JAUDIOS_RT_CHECK=ON) whose binaries interpose
# malloc/free/operator new/delete (+ pthread_mutex_lock on glibc) and count every call made
# inside an rt::ScopedRealtimeSection (opened by AudioEngine::renderBlock).
#
# Fails if the audio path allocates or frees. Mutex locks are reported: JUCE's
# AudioProcessorGraph takes each node's uncontended callback lock, which we cannot remove
# without replacing the graph.
#
# usage: tools/rt_check.sh [audio-file]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${RT_BUILD_DIR:-$ROOT/build-rt}"
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)

cmake -S "$ROOT" -B "$BUILD" "${GEN[@]}" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DJAUDIOS_RT_CHECK=ON -DJAUDIOS_BUILD_APP=OFF >/dev/null
cmake --build "$BUILD" --target jaudios_rt_tests jaudios_plugin_rt_tests app-cli-rt

echo "== [rt] unit tests (instrumented) =="
"$BUILD/tests/jaudios_rt_tests" "[rt]" --success --reporter compact
echo "== [rt] VST3 hosting wrapper around the in-repo test plugin (instrumented) =="
"$BUILD/tests/jaudios_plugin_rt_tests" "[rt]" --success --reporter compact

FILE="${1:-$ROOT/tests/fixtures/house_124_Amin.wav}"
if [[ -f "$FILE" ]]; then
    echo "== render through engine graph (instrumented): $FILE =="
    CLI="$(find "$BUILD/tools/cli" -type f -name app-cli-rt -perm -u+x | head -n1)"
    OUT="$("$CLI" render "$FILE" --block 128)"
    echo "$OUT"
    python3 - "$OUT" <<'PY'
import json, sys
rt = json.loads(sys.argv[1])["rt"]
print(f"rt_check: allocations={rt['allocations']} deallocations={rt['deallocations']} mutex_locks={rt['mutex_locks']}")
if rt["allocations"] or rt["deallocations"]:
    print("rt_check: FAIL (allocation on audio thread)"); sys.exit(1)
print("rt_check: PASS (zero allocations)" + ("; NOTE: JUCE graph node locks present" if rt["mutex_locks"] else ""))
PY
else
    echo "rt_check: fixture $FILE missing; unit tests only"
fi
