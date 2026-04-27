#!/usr/bin/env bash
set -euo pipefail

BENCH=./build/bench
MSGS=${1:-2000000}
OUTDIR=results

if [[ ! -x "$BENCH" ]]; then
    echo "Build first: cmake --build build"
    exit 1
fi

mkdir -p "$OUTDIR"

echo "=== no affinity ===" | tee "$OUTDIR/bench_no_affinity.txt"
"$BENCH" --msgs "$MSGS" --warmup 200000 2>&1 | tee -a "$OUTDIR/bench_no_affinity.txt"

if command -v taskset &>/dev/null; then
    echo "=== pinned to core 3 ===" | tee "$OUTDIR/bench_with_affinity.txt"
    taskset -c 3 "$BENCH" --msgs "$MSGS" --warmup 200000 2>&1 \
        | tee -a "$OUTDIR/bench_with_affinity.txt"
else
    echo "taskset not available (macOS); run on Linux to capture pinned results"
fi
