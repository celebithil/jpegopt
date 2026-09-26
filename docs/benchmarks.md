# Benchmarks

## Headline numbers

Best-of-N over all candidates; every candidate pixel-verified; 0 errors in all
four runs. Measured with the 1.0.0 binary; artifacts in
[`bench/`](../bench/summary.md).

| corpus | files | Huffman only | + arithmetic | best Huffman | best arithmetic |
|---|---|---|---|---|---|
| mixed (COCO 2017 + Open Images v7) | 3 954 | **5.559%** | **10.900%** | `progressive-mozmax-al4` (1 167) | `progressive+arith-al6-dcplain` (1 635) |
| phone photos, 12 MP (realme 15 5G) | 144 | **7.375%** | **14.124%** | `progressive-mozmax-al4` (144/144) | `progressive+arith-al6-dcplain` (144/144) |

On the phone-photo corpus a single candidate won all 144 files in both modes.
Camera JPEGs (baseline, lightly entropy-coded) leave more headroom than web
images, which are usually already well optimised — hence the higher percentages.

guetzli won 5 of 3 954 files in the mixed Huffman run and 0 in the arithmetic
run; it never won on the camera photos. It is retained as an independent
re-encode path, not because it contributes materially to the average.

## What "Huffman only" means

Both columns come from a **single** run that includes `--arith`. The Huffman
column is recomputed afterwards: for each file, take the smallest candidate whose
tag does not contain `arith`, falling back to the original size.

This shortcut was **validated** on the current data: recomputing the Huffman
column from the mixed `--arith` run gives 5.559%, exactly equal to a dedicated
run of the same corpus without `--arith` (5.559%). `scripts/analyze_bench.py`
implements the same rule.

## Method

```sh
# 1. fetch and flatten the corpora (once, large)
scripts/corpora_download.sh ~/jpeg_corpora
scripts/flatten_corpus.sh   ~/jpeg_corpora

# 2. build a file list
find ~/jpeg_corpora/all -name '*.jpg' > list.txt

# 3. run the search, one JSON per list
scripts/run_bench.sh 8 list.txt        # -> /tmp/bench_list.json

# 4. summarise (per-method totals, KEEP/DROP, huff-only vs arith)
python3 scripts/analyze_bench.py  /tmp/bench_list.json
python3 scripts/bench_summary.py "mixed=/tmp/bench_list.json"
```

`run_bench.sh` invokes `jpegopt --dry-run --json --show-all --arith`, so nothing
is written and every candidate size is recorded. Throughput depends heavily on
the host: the 3 954-file mixed sample took about 25 minutes at 6 threads on a
shared 16-core machine under heavy external load. Treat wall-clock as
environment-specific, not as a benchmark.

### Reading the numbers correctly

- **`best_size` equals `original_size` when nothing won**, so savings are never
  negative and per-file percentages are meaningful.
- **`candidates` lists verified candidates only.** A method missing from the
  output either was not run or failed verification — do not read absence as
  "not attempted".
- **The KEEP/DROP verdict is what shaped the tool.** A candidate is kept only if
  it won at least one file corpus-wide; that rule produced the current 23
  Huffman and 20 arithmetic scan scripts
  ([scan-scripts.md](scan-scripts.md)). `scripts/bench_summary.py` prints the
  verdict for any run.

## Corpora and licensing

The measurement corpora are **not** part of this repository and are not
redistributed with it:

| corpus | composition | licence |
|---|---|---|
| mixed sample | 3 954 of 205 563 files from `jpeg_corpora/all` (3 153 COCO 2017, 801 Open Images v7 validation); deterministic every-52nd-file stride over the sorted list, which preserves the 79.8 / 20.2 source split | COCO terms of use / CC BY 2.0 — images carry individual licences |
| phone144 | 144 photographs from a realme 15 5G (3072×4096, baseline sequential, EXIF + XMP + ICC) | local personal corpus |
| mozjpeg test images | 4 files, smoke input only | mozjpeg testimage terms |
| Flickr subset (optional) | requires `FLICKR_KEY` | per-photo CC licences |

`scripts/corpora_download.sh` and `scripts/flatten_corpus.sh` fetch and
deduplicate them locally; `.gitignore` keeps them out of version control. If you
publish a benchmark, state which corpora you used and under what licence you
obtained them.

## Reproducibility

The committed artifacts are the per-file reports for the small corpus and
aggregates for all four runs; see [`bench/summary.md`](../bench/summary.md) for
the exact commands, the host, and why the large per-file dumps are not committed.

The figures above are stable enough to guide expectations but are not a
specification. Re-running on a different corpus will change both the percentages
and the KEEP/DROP verdict, and may justify re-tuning the candidate lists.
