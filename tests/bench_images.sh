#!/usr/bin/env bash
# Regression bench over the real camera photos in Images/ (skips if absent).
# Aggregates per-method candidate totals from the --dry-run --json output.
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -d Images ]]; then
    echo "Images/ not present; skipping bench."
    exit 0
fi

THREADS="${1:-4}"
OUT="$(mktemp --suffix=.json)"

trap 'rm -f "$OUT"' EXIT

./build/jpegopt --dry-run --json -t "$THREADS" Images > "$OUT"

python3 - "$OUT" <<'PY'
import json, sys

with open(sys.argv[1]) as f:
    results = json.load(f)

if isinstance(results, list):
    items = results
elif "files" in results:
    items = results["files"]
elif "results" in results:
    items = results["results"]
else:
    items = list(results.values())

methods = {}
orig = best = 0
errs = 0
for r in items:
    if not r.get("ok"):
        errs += 1
        continue
    orig += r.get("original_size", 0)
    best += r.get("best_size", 0)
    for m, s in (r.get("candidates") or {}).items():
        methods.setdefault(m, 0)
        methods[m] += s

print(f"files: {len(items)}  errors: {errs}")
print(f"original: {orig:,}  best: {best:,}  ({100 * (1 - best / orig):.2f}%)")
for m in sorted(methods, key=lambda m: -methods[m]):
    s = methods[m]
    print(f"  {m:18s} {s:15,d}  ({100 * (1 - s / orig):+.2f}%)")
PY
