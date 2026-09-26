# Changelog

All notable changes to this project are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
