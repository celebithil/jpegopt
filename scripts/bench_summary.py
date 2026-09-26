#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarise jpegopt --json benchmark runs into a report.

Reads one or more JSON reports produced by `jpegopt --dry-run --json --show-all`
and prints, per corpus:

  * file counts and error counts
  * overall savings for the run as it was executed
  * the "Huffman only" figure, recomputed as the smallest non-arithmetic
    candidate per file (an arith run therefore yields both columns)
  * a per-method table with win counts, sorted by total size
  * the KEEP/DROP verdict, i.e. which methods won at least one file

Usage:
    scripts/bench_summary.py LABEL=run1.json [LABEL=run2.json ...]
"""

import json
import sys
from collections import Counter


def load(path):
    with open(path) as f:
        data = json.load(f)
    if isinstance(data, dict):
        data = data.get("files", data.get("results", []))
    return data


def summarise(label, items):
    ok = [r for r in items if r.get("ok")]
    errors = len(items) - len(ok)
    original = sum(r.get("original_size", 0) for r in ok)
    best = sum(r.get("best_size", 0) for r in ok)
    arith_best = 0
    for r in ok:
        cand = r.get("candidates") or {}
        sizes = [s for m, s in cand.items() if "arith" in m]
        arith_best += min(sizes) if sizes else r["original_size"]
    huff_best = 0
    for r in ok:
        cand = r.get("candidates") or {}
        sizes = [s for m, s in cand.items() if "arith" not in m]
        huff_best += min(sizes) if sizes else r["original_size"]

    methods = {}
    wins = Counter()
    for r in ok:
        for m, s in (r.get("candidates") or {}).items():
            e = methods.setdefault(m, {"bytes": 0, "files": 0})
            e["bytes"] += s
            e["files"] += 1
        if r.get("method"):
            wins[r["method"]] += 1

    def pct(v):
        return 100.0 * (1.0 - v / original) if original else 0.0

    print(f"## {label}")
    print()
    print(f"files: {len(items)}  errors: {errors}  ok: {len(ok)}")
    print(f"original bytes: {original:,}")
    print(f"run as executed: {pct(best):.3f}%  (huff-only: {pct(huff_best):.3f}%, "
          f"arith-only: {pct(arith_best):.3f}%)")
    print()
    print("| method | files | wins | bytes | sav% |")
    print("|---|---:|---:|---:|---:|")
    for m, e in sorted(methods.items(), key=lambda kv: kv[1]["bytes"]):
        print(f"| `{m}` | {e['files']} | {wins.get(m, 0)} | {e['bytes']:,} | "
              f"{pct(e['bytes']):.3f} |")
    keep = sorted(m for m, c in wins.items() if c > 0)
    drop = sorted(set(methods) - set(keep))
    print()
    print(f"KEEP ({len(keep)}): {', '.join(keep)}")
    print()
    print(f"DROP ({len(drop)}): {', '.join(drop)}")
    print()
    return {
        "label": label,
        "files": len(items),
        "errors": errors,
        "original_bytes": original,
        "savings_pct_run": round(pct(best), 3),
        "savings_pct_huff_only": round(pct(huff_best), 3),
        "savings_pct_arith_only": round(pct(arith_best), 3),
        "methods": {m: {"files": e["files"], "wins": wins.get(m, 0),
                        "bytes": e["bytes"]} for m, e in methods.items()},
        "keep": keep,
    }


def main(argv):
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    results = []
    for arg in argv:
        if "=" in arg:
            label, path = arg.split("=", 1)
        else:
            label, path = arg, arg
        results.append(summarise(label, load(path)))
    if len(results) == 1 and "--json" in sys.argv:
        json.dump(results[0], sys.stdout, indent=2)
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
