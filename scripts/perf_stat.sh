#!/usr/bin/env bash
# Runs the benchmark under perf stat and captures IPC, cache-miss rate,
# and branch-miss rate — the three metrics cited in the resume.
# Requires Linux with perf installed and optionally CAP_SYS_ADMIN.
#
# Usage: ./scripts/perf_stat.sh [--msgs N]

set -euo pipefail

MSGS=${2:-2000000}
BENCH=./build/bench

if [[ ! -x "$BENCH" ]]; then
    echo "Build first: cmake --build build"
    exit 1
fi

echo "Running perf stat on $BENCH with $MSGS messages..."

perf stat \
    -e instructions,cycles,cache-misses,cache-references,branch-misses,branches \
    --repeat 5 \
    -- "$BENCH" --msgs "$MSGS" --warmup 200000

# Derived rates printed by perf stat automatically:
#   IPC = instructions / cycles
#   cache-miss rate = cache-misses / cache-references * 100
#   branch-miss rate = branch-misses / branches * 100
