#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
threads="${1:-8}"
shift
for s in "$@"; do
  out="/tmp/bench_$(basename "$s" .txt).json"
  echo "[$(date +%H:%M:%S)] start $s -> $out (t=$threads)"
  ./build/jpegopt --dry-run --json --show-all --arith -t "$threads" $(< "$s") \
    > "$out" 2> "${out%.json}.err" || true
  echo "[$(date +%H:%M:%S)] done $s: $(stat -c%s "$out") bytes"
done
