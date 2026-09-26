# Benchmark artifacts

Machine-readable results for the figures quoted in
[`docs/benchmarks.md`](../docs/benchmarks.md) and the README.

## What was measured

`jpegopt --dry-run --json --show-all [--arith]` over two corpora, with the
1.0.0 binary. Nothing was written; every reported candidate was verified
pixel-identical before it was counted.

| corpus | files | errors | Huffman only | + arithmetic |
|---|---:|---:|---:|---:|
| mixed corpus (COCO 2017 + Open Images v7) | 3954 | 0 | 5.559% | 10.900% |
| phone photos (realme 15 5G, 12 MP) | 144 | 0 | 7.375% | 14.124% |

The Huffman column is recomputed from the `--arith` run by taking, per file, the
smallest candidate whose tag does not contain `arith`. On this data the
recomputed figure is **exactly** equal to a dedicated run without `--arith`
(5.559% vs 5.559% on the mixed corpus), which validates the shortcut that
`scripts/analyze_bench.py` and `docs/benchmarks.md` rely on.

## Files

| File | Contents |
|---|---|
| `methods.json` | Aggregates for all four runs: totals, savings, per-method file counts, win counts and byte totals. Start here. |
| `phone144-arith.json` | Full per-file report for the 144 camera photos with `--arith`. |
| `phone144-huff.json` | Same photos, Huffman only. |
| `summary.md` | Human-readable tables rendered from `methods.json`. |

The per-file reports for the mixed corpus are **not** committed: they are 7.0 MB
(arithmetic) and 4.0 MB (Huffman) of per-file detail, and `methods.json` carries
everything needed to reproduce the tables. Regenerate them with the command
below.

## Reproducing

```sh
# mixed corpus sample: deterministic stride over the flattened corpus,
# preserving the coco:openimages ratio
ls jpeg_corpora/all | sort | awk 'NR % 52 == 1' \
  | sed 's|^|jpeg_corpora/all/|' > /tmp/mix.txt

./build/jpegopt --dry-run --json --show-all --arith -t 6 $(cat /tmp/mix.txt) \
  > bench/mixed3954-arith.json
./build/jpegopt --dry-run --json --show-all        -t 6 $(cat /tmp/mix.txt) \
  > bench/mixed3954-huff.json

# camera photos
./build/jpegopt --dry-run --json --show-all --arith -t 6 photochki18+/* \
  > bench/phone144-arith.json
./build/jpegopt --dry-run --json --show-all        -t 6 photochki18+/* \
  > bench/phone144-huff.json

python3 scripts/bench_summary.py \
  "mixed3954=bench/mixed3954-arith.json" \
  "phone144=bench/phone144-arith.json"
```

## Corpora

Neither corpus is redistributed with this project; both are produced locally by
`scripts/corpora_download.sh` and `scripts/flatten_corpus.sh`.

- **mixed** — `jpeg_corpora/all`: 205 563 files (COCO 2017 images, Open Images
  v7 validation images) deduplicated by SHA-256. The sample is every 52nd file
  in sorted order, giving 3 954 files (3 153 COCO, 801 Open Images — the
  79.8 / 20.2 split of the full corpus). COCO and Open Images images carry
  their own licences and are **not** part of this repository.
- **phone144** — 144 photographs from a realme 15 5G (3072x4096, baseline
  sequential, with EXIF/XMP/ICC), a local personal corpus, also not
  redistributed.

## Environment

Measured on a shared 16-core Linux host (x86-64, glibc), 6 worker threads,
CMake `Release` build of jpegopt 1.0.0 with the vendored libjpeg-turbo compiled
with SIMD. Absolute timings are not comparable across machines; the savings
percentages are.

The mixed-corpus runs took roughly 25 minutes each on this (heavily
oversubscribed) host. Do not read the wall-clock as a performance benchmark.

## guetzli

guetzli won 5 of 3954 files in the mixed Huffman run and 0 of 3954 in the arithmetic run; it never won on the camera photos. It is retained as an independent re-encode path, not because it moves the average.
