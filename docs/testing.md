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

**JSON schema.** `test_json_schema` runs the pipeline on a good and a bad file,
renders `--json` and asserts the documented schema (docs/json-output.md): one
object per input, in order, every documented key present with the right type,
a non-empty `error` and empty `candidates` for the failed file, and at least one
verified candidate for the good one.

**CLI exit codes.** `test_cli_exit_codes` drives the built `jpegopt` binary and
checks `0` on success (`--version`, `--help`, a normal run), `2` on usage errors
(unknown option, no inputs, no JPEG found in the inputs) and `1` when an input
exists but cannot be processed. The binary is located next to the test binary, so
this test runs after `roundtrip_test` is built with the tool; it is skipped with
a note if `jpegopt` is absent.

**CLI contract extras.** `test_cli_contract_extras` pins the boundaries added
after review: `-t` accepts only non-negative integers (`abc`/`2x`/`-3` → exit 2)
and clamps values above the cap instead of rejecting them; `method`/`arith` are
empty/`false` exactly when nothing was selected (including the threshold case);
and `--strip-metadata` fills `stripped_markers` with the categories it removes.

**Edge inputs.** `test_edge_inputs` feeds the pipeline an empty file, a truncated
JPEG, non-JPEG bytes under a `.jpg` name, a 16×16 image and a grayscale source,
and checks the first three are reported as errors rather than crashes, that tiny
inputs never grow, and that a grayscale result stays lossless.

**CMYK sources.** `test_cmyk_input` encodes a 4-component `JCS_CMYK` fixture and
checks it is optimized losslessly in its own 4-channel space: the result must be
`ok`, decode back to 4 channels at 8-bit, and be sample-for-sample identical to
the source. The verification deliberately does not convert to RGB — that
conversion is what used to make CMYK fail.

**12-bit sources.** `test_twelve_bit_input` runs the vendored 12-bit image through
the pipeline and asserts: it is accepted; it decodes at 12-bit precision; exactly
one candidate (`progressive-12bit`) is produced and it is not arithmetic-coded;
and the winner preserves precision 12 and is sample-for-sample identical. The
test is skipped with a note if the fixture is absent.

**File lists.** `test_file_lists` covers `--files-from` and `--files-stdin`: the
list grammar (one path per line, blank lines and `#` comments skipped, order and
trimming preserved) is asserted directly against `parse_cli`, an unreadable list
file is a parse error, a listed path that does not exist is skipped rather than
fatal, and both flags are driven through the binary end-to-end.

## What it does not cover

Known gaps, listed so contributors can prioritise:

- **No real photographs in CI.** Fixtures are synthetic 320×240 and 640×480 RGB
  gradients. The regression bench `tests/bench_images.sh` exercises real photos
  but is not part of CTest and needs an `Images/` directory.
- **No golden/size-regression assertions** — the suite verifies correctness
  (lossless), not "the output is N bytes". Size is tuned by the corpus bench.
- **No 16-bit source test.** 16-bit JPEG is rejected by the decoder; the
  rejection path is exercised only indirectly.
- **No CMYK corpus benchmarks.** CMYK and 12-bit coverage is functional (one
  synthetic CMYK fixture plus the vendored 12-bit image), not statistical.

## Adding a test

Add a `CHECK(...)` in the appropriate test function in
`tests/roundtrip_test.cpp`. When you add a new candidate, also add its tag to
the `required` list so the suite fails if it does not appear. When you add a new
scan style, add it to the `sweep_styles` array so it is exercised.

For behaviour that is a property of the process rather than of the library
(exit codes, `--json`/`--help`/`--version` output, file lists), use the
`run_tool` / `run_tool_stdin` helpers, which drive the built `jpegopt` binary;
the test binary locates it next to itself and skips those checks with a note if
it is absent. For input grammar, prefer an in-process `parse_cli` assertion so a
regression cannot hide behind paths that merely fail to exist.

Prefer a synthetic fixture for correctness (deterministic, no binary blobs in
the repo) and the bench script for size.

## Regression bench

`tests/bench_images.sh [threads]` runs the tool over `Images/` (skipped if the
directory is absent) and aggregates per-method candidate totals from the JSON
output. It is a size/quality regression check, not a pass/fail test.
