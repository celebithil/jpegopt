# `--json` output format

`jpegopt --json` writes a single JSON **array** to stdout, one object per input
file, in the same order as the inputs were processed. Nothing else is written to
stdout; human-readable output is suppressed entirely.

The format is stable and is the basis of `scripts/analyze_bench.py`.

## Example

```console
$ jpegopt --dry-run --json photo.jpg
```

```json
[
  {
    "path": "photo.jpg",
    "ok": true,
    "changed": true,
    "strip_metadata": false,
    "stripped_markers": "",
    "original_size": 3600951,
    "best_size": 3376432,
    "method": "progressive-mozmax-al4",
    "arith": false,
    "arith_fell_back": false,
    "error": "",
    "candidates": {
      "progressive": 3430968,
      "progressive-min": 3449315,
      "progressive-bands": 3510037,
      "guetzli": 3600951
    }
  }
]
```

## Keys

| Key | Type | Meaning |
|---|---|---|
| `path` | string | The input path, exactly as resolved from the command line. |
| `ok` | bool | `false` if the file could not be processed at all (unreadable, not a JPEG, decoder failure). See `error`. |
| `changed` | bool | `true` when a verified, smaller candidate was **selected**. This is also `true` under `--dry-run`, where nothing is written — it means "an improvement was found", not "a file was rewritten". |
| `strip_metadata` | bool | `true` when the run used `--strip-metadata` (all markers removed). |
| `stripped_markers` | string | Comma-separated list of marker categories the policy *would* remove from the source: `jfif`, `jfxx`, `exif`, `xmp`, `icc`, `iptc`, `adobe`, `com`. Empty when no category matched. Under `--strip-metadata` the source is classified against an all-categories policy, so this lists every strippable category present. See note below. |
| `original_size` | int | Size of the input file in bytes. |
| `best_size` | int | Size of the smallest verified candidate, **or `original_size` when nothing beat the source**. Never larger than `original_size`. |
| `method` | string | Tag of the winning candidate (e.g. `progressive-mozmax-al4`), or empty when no smaller candidate was **selected** — which includes the threshold case. See [scan-scripts.md](scan-scripts.md) for the tag vocabulary. |
| `arith` | bool | `true` when the winner is an arithmetic-coded candidate. `false` whenever `method` is empty. |
| `arith_fell_back` | bool | `true` when an arithmetic candidate existed but was **not** used because it was not strictly smaller than the best Huffman candidate. |
| `error` | string | Human-readable error text; empty when `ok` is `true`. |
| `candidates` | object | Map of candidate tag → size in bytes, for every **verified** candidate. |

## Semantics that matter for scripts

**`best_size` is not always the size of a written file.** When no candidate beat
the source, `best_size == original_size`. Compute savings as
`1 - best_size / original_size`; it is never negative.

**`changed` means "improvement found", not "file written".** It is `true` under
`--dry-run` as well, because the field is set when a smaller verified candidate
is selected, and the write is a separate step that `--dry-run` skips. To learn
what would hit the disk, use `changed`; to measure, use `best_size`; to detect
actual disk activity in a real run, compare file mtimes or use `--dry-run`
first.

**`candidates` contains verified candidates only.** A candidate that failed
pixel verification is dropped silently and does not appear here
(`add_if_valid` in `src/pipeline.cpp`). The absence of a tag therefore means "no verified
result", which is *not* the same as "the method was never run".

**`--show-all` does not change JSON.** The `candidates` map is always present in
JSON mode; the flag only affects the human-readable text report.

**`stripped_markers` is a prediction, not a receipt.** It is computed by
classifying the *source* against the requested policy, independently of which
candidate won and whether anything was written
(the `classify_strippable` call in `process_file`). Under `--strip-metadata` the
policy used for this prediction is "all categories", so the field lists what the
fast path would drop rather than staying empty.

**`candidates` is the per-file cost of the search.** Its size is the number of
candidates that produced a verified result — useful for spotting inputs where
most methods failed.

## Consumer notes

- `candidates` is an object with unique keys, so a candidate tag appears at most
  once per file.
- Sizes are plain JSON integers (bytes).
- Parse the whole stream: with many files the array is large but still emitted
  in one document. If you need line-delimited output, use `--dry-run` per file
  or post-process with `jq`.
