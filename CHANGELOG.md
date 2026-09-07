# Changelog

## [1.0.0] - 2026-09-07

Initial release.

### Features

- **Lossless optimization pipeline.** JPEGs are re-encoded with a battery of
  progressive scan scripts and entropy-coding modes; every candidate is
  decoded and verified pixel-identical to the source (TurboJPEG independent
  decoder + libjpeg-turbo lossless transcode) before it is counted.
- **23 progressive-Huffman scan scripts**, including the mozjpeg *max /
  default / fast* families (`src/transcoder.cpp`).
- **Guetzli candidate** — independent sequential encoder with cost-clustered
  Huffman tables.
- **Optional arithmetic coding** — 21 progressive + 1 sequential scripts.
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

### Build and packaging

- Self-contained repository: libjpeg-turbo and guetzli are vendored in-tree
  under `vendor/` — no submodules, no downloads. Clone and build.
- CMake build with out-of-tree libjpeg-turbo and SIMD fallback to C
  intrinsics (nasm optional); `ctest` roundtrip suite.
- CI on GitHub Actions (Ubuntu): configure, build, test.