#!/usr/bin/env python3
"""Analyze jpegopt --json --show-all output.

Usage: analyze_bench.py FILE.json [FILE2.json ...]

Reports per-method stats and applies the retention rule:
keep a candidate iff it won >=1 file across the whole corpus.
Prints the keep/drop lists and both Huffman-only and full (arith) totals.
"""
import json
import sys


def load(paths):
    items = []
    for p in paths:
        with open(p) as f:
            r = json.load(f)
        files = r["files"] if isinstance(r, dict) and "files" in r else r
        items.extend(files)
    return items


def main():
    items = load(sys.argv[1:])
    ok = [i for i in items if i.get("ok")]
    errs = [i for i in items if not i.get("ok")]
    if errs:
        print("ERRORS:")
        for e in errs[:20]:
            print("  %s: %s" % (e.get("path"), e.get("error")))

    orig = sum(i["original_size"] for i in ok)
    best = sum(i["best_size"] for i in ok)
    print("files: %d (errors: %d)" % (len(ok), len(errs)))
    print("original: %d  best: %d  savings: %.3f%%" % (orig, best, 100 * (1 - best / orig)))

    wins = {}
    for i in ok:
        if i.get("method"):
            wins[i["method"]] = wins.get(i["method"], 0) + 1

    huff = 0
    for i in ok:
        cand = i.get("candidates") or {}
        hb = min((s for m, s in cand.items() if "arith" not in m), default=orig)
        huff += hb
    print("Huffman-only best: %d  savings: %.3f%%" % (huff, 100 * (1 - huff / orig)))
    print("arith adds: +%.3f pct points" % (100 * (1 - best / orig) - 100 * (1 - huff / orig)))

    tot = {}
    for i in ok:
        for m, s in (i.get("candidates") or {}).items():
            t = tot.setdefault(m, [0, 0])
            t[0] += 1
            t[1] += s

    print("\n%-44s %6s %14s %9s %6s" % ("method", "files", "bytes", "sav%", "wins"))
    for m, (n, b) in sorted(tot.items(), key=lambda x: x[1][1]):
        print("%-44s %6d %14d %8.3f%% %6d" % (m, n, b, 100 * (1 - b / orig), wins.get(m, 0)))

    keep = sorted(w for w in wins if wins[w] >= 1)
    drop = sorted(m for m in tot if wins.get(m, 0) == 0)
    print("\nKEEP (%d): %s" % (len(keep), ", ".join(keep)))
    print("DROP (%d): %s" % (len(drop), ", ".join(drop)))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main()
