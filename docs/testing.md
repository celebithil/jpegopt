# Testing

## The suite

One CTest target, `roundtrip`, built from `tests/roundtrip_test.cpp`:

```sh
cmake -S . -B build -DJPEGOPT_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The test is a single binary with a `CHECK` macro; every failure is printed with
file and line, the run continues, and the process exits non-zero if anything
failed.

## What it covers

**Scan scripts.** Every `ScanStyle` is exercised through a `sweep` loop: each is
transcoded, checked to be progressive, and checked to be pixel-identical to the
source. The default pipeline candidates are additionally checked for presence in
the `candidates` list, so a typo or a broken builder fails the suite
(the `required` list in `test_pipeline`).

**Lossless transcode.** baseline→progressive, baseline→optimized baseline,
progressive→arithmetic, and each individual style, all verified against a
TurboJPEG reference decode.

**Source-script preservation.** The progressive fixture's own scan script is
extracted with `extract_scan_script`, re-applied via `TranscodeOptions::scan_override`,
and the output is checked to be both lossless and structurally identical in scan
count and per-scan parameters.

**guetzli.** Re-encodes baseline and progressive fixtures, checks the output is
sequential Huffman (not progressive, not arithmetic), and checks it is lossless.

**Restart markers.** A fixture with a DRI interval is re-encoded with
`strip_restart`, checked to have no DRI, to be smaller, and to be lossless.

**Metadata.** A fixture carrying a fake EXIF APP1 and a COM marker is checked to
keep both by default, to lose both with `--strip-metadata`, and to stay lossless
in each case. The granular path is checked: strip EXIF only removes APP1 and
keeps COM; strip COM only removes COM and keeps APP1; an empty policy leaves the
bytes byte-identical; classify reports the expected categories; and a
pipeline-level test injects a large EXIF segment, confirms the output has no
APP1 but keeps COM, and confirms the result is still pixel-identical.

**Threshold.** A 90% threshold blocks the write (`changed == false` and no output
file is created), while a normal run writes.

**Arithmetic.** Sequential and progressive arithmetic outputs are checked to be
flagged as arithmetic and to decode identically.

## What it does not cover

Known gaps, listed so contributors can prioritise:

- **No real photographs in CI.** Fixtures are synthetic 320×240 and 640×480 RGB
  gradients. The regression bench `tests/bench_images.sh` exercises real photos
  but is not part of CTest and needs an `Images/` directory.
- **No test for `--files-from` / `--files-stdin`.**
- **No schema test for `--json`.** The JSON surface is documented in
  [json-output.md](json-output.md) but not asserted by the suite.
- **No exit-code test.**
- **No edge-case inputs:** empty file, truncated JPEG, JPEG with EXIF only,
  grayscale and CMYK sources, 12-bit (unsupported), very small images.
- **No golden/size-regression assertions** — the suite verifies correctness
  (lossless), not "the output is N bytes". Size is tuned by the corpus bench.

## Adding a test

Add a `CHECK(...)` in the appropriate test function in
`tests/roundtrip_test.cpp`. When you add a new candidate, also add its tag to
the `required` list so the suite fails if it does not appear. When you add a new
scan style, add it to the `sweep_styles` array so it is exercised.

Prefer a synthetic fixture for correctness (deterministic, no binary blobs in
the repo) and the bench script for size.

## Regression bench

`tests/bench_images.sh [threads]` runs the tool over `Images/` (skipped if the
directory is absent) and aggregates per-method candidate totals from the JSON
output. It is a size/quality regression check, not a pass/fail test.
