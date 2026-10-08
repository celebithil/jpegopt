# Changelog

All notable changes to this project are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-10-08  ([tag](https://github.com/celebithil/jpegopt/releases/tag/v1.1.0))

Feature release: new source classes (CMYK, 12-bit) plus a batch of correctness
fixes found by a code and sanitizer review.

### Added

- **CMYK support.** CMYK sources are now optimized losslessly in their own
  4-channel colour model instead of being rejected. Verification decodes a
  candidate back into the source's own colour model and bit depth rather than
  converting to RGB, which is what made CMYK fail before with
  `Unsupported color conversion request`.
- **12-bit support.** 12-bit-per-sample JPEG is optimized through TurboJPEG's
  transform path (`progressive-12bit` candidate): lossless, precision preserved,
  verified at 12-bit depth. The vendored build has no 12-bit coefficient API, so
  such sources get exactly one candidate; `--arith` and the scan-script family do
  not apply to them.
- **`method` and `arith`** in the JSON report are now empty/`false` whenever no
  smaller candidate was selected, matching the documented contract.
- **`stripped_markers` is populated under `--strip-metadata`** with the
  categories that would be removed, instead of being reported empty.
- Sanitizer builds now instrument the vendored libjpeg-turbo as well
  (`CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` are forwarded to the external project), so
  the CI `sanitizers` job also covers the decoder that parses untrusted input.

### Fixed

- **A bare relative filename could not be written.** `jpegopt photo.jpg` (no
  directory component) staged its temporary file in the filesystem root and
  failed with `cannot open for writing /.jpegopt.tmp...`; `./photo.jpg`,
  `sub/photo.jpg` and absolute paths were unaffected. The temp file now stays
  beside the target.
- **`read_jpeg_info` treated DHT and JPG segments as frame headers**, because it
  tested the contiguous `0xC0..0xCB` range. DHT precedes SOF in a large share of
  real files, so bogus dimensions/component counts were reported. `0xC4`, `0xC8`
  and `0xCC` are now excluded.
- **Out-of-bounds read in `extract_scan_script`** on a truncated SOS segment
  (off-by-one in the length guard); found by AddressSanitizer.
- **`-t/--threads` was unvalidated:** `-t abc` silently selected the automatic
  pool and very large values spawned that many workers. It now requires a
  non-negative integer and is clamped to a ceiling of 64 workers.
- `read_file` now checks the seek back to the start of the file.
- Verbose output no longer claims "via <method>" for a file that was left
  unchanged.

### Documentation

- Corrected the `kMozFast` H/A column and the `kFewScanAl3` example in
  `docs/scan-scripts.md`; replaced a dangling `bench/README.md` reference;
  documented the in-place inode/hardlink behaviour and the thread ceiling.
- Removed the non-existent `WITH_12BIT` CMake option (libjpeg-turbo 3.0.x ships
  12-bit unconditionally).
- Removed the unused `util::file_size` / `util::self_path` helpers.

### Tests

- New coverage for the JSON schema, CLI exit codes, `-t` bounds, `--files-from`
  / `--files-stdin`, edge inputs, CMYK, 12-bit and the regressions above.

## [1.0.0] - 2026-09-07  ([tag](https://github.com/celebithil/jpegopt/releases/tag/v1.0.0))

First public release. Consolidates the internal 0.1.x/0.2.x history below.

### Features

- **Lossless optimization pipeline.** JPEGs are re-encoded with a battery of
  progressive scan scripts and entropy-coding modes; every candidate is decoded
  and verified pixel-identical to the source (TurboJPEG independent decoder +
  libjpeg-turbo lossless transcode) before it is counted.
- **23 progressive-Huffman scan scripts**, including the mozjpeg *max / default /
  fast* families (`src/transcoder.cpp`).
- **Guetzli candidate** — independent sequential encoder with cost-clustered
  Huffman tables.
- **Optional arithmetic coding** — 20 progressive + 1 sequential scripts.
- **Granular marker stripping**: `--strip-exif`, `--strip-xmp`, `--strip-icc`,
  `--strip-iptc`, `--strip-adobe`, `--strip-jfif`, `--strip-jfxx`,
  `--strip-com`, `--strip-metadata` (all).
- **`-T, --threshold N`** — keep the original unless savings reach N%.
- **`--files-from FILE`** and **`--files-stdin`** — process a file list.
- **`-p, --preserve`** — copy source timestamps/mode to fresh outputs.
- **`--json`** — machine-readable report.
- **`--show-all`** — list every candidate size per file.
- **`--dry-run`** — report only, do not write anything.
- **Multi-threading** with automatic thread pool sizing.
- **In-place mode** (`-i`) that overwrites the source with the best result.
- **`-V, --version`**.

### Build and packaging

- Self-contained repository: libjpeg-turbo and guetzli are vendored in-tree
  under `vendor/` — no submodules, no downloads. Clone and build.
- CMake build with out-of-tree libjpeg-turbo and SIMD fallback to C intrinsics
  (nasm optional); `ctest` roundtrip suite.
- CI on GitHub Actions (Ubuntu): configure, build, test.
- Apache-2.0 licensing with full `NOTICE` attribution for libjpeg-turbo,
  guetzli and mozjpeg-derived scan scripts.

## [0.2.0]  ([tag](https://github.com/celebithil/jpegopt/releases/tag/v0.2.0))

### Added

- **Granular marker stripping.** Per-category APP/COM removal
  (`--strip-exif`, `--strip-xmp`, `--strip-icc`, `--strip-iptc`,
  `--strip-adobe`, `--strip-jfif`, `--strip-jfxx`, `--strip-com`) implemented as
  a post-encode pass over the header region; `--strip-metadata` keeps its
  fast path where markers are never copied. Pixel verification is unchanged.
- **`-T, --threshold N`** — a result is written only when it saves at least N%
  of the original size.
- **`--files-from FILE`** and **`--files-stdin`** — read the list of files to
  process from a file or standard input; blank lines and `#` comments ignored.
- **Timestamp preservation** — in-place writes keep the original mtime and mode;
  `-p, --preserve` extends this to freshly created `<name>.opt.<ext>` outputs.
- Text and JSON reports now list which marker categories were stripped
  (`stripped_markers`).

## [0.1.0]  ([tag](https://github.com/celebithil/jpegopt/releases/tag/v0.1.0))

### Added

- Initial lossless pipeline: 23 progressive-Huffman scan scripts plus a guetzli
  candidate (24 by default), all pixel-verified, best-of-N selection.
- `--arith` adds 20 progressive + 1 sequential arithmetic-coding candidates
  (45 total) with the rule that an arithmetic result is used only when strictly
  smaller than the best Huffman result.
- `--dry-run`, `--show-all`, `--temp-dir`, `--strip-metadata`, `--json`,
  multi-threading, in-place mode.
- `roundtrip_test` suite covering every scan script, guetzli, restart-marker
  stripping, metadata handling and arithmetic mode.
