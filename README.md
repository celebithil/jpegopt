# jpegopt

Lossless JPEG optimizer. Re-encodes JPEGs with a battery of progressive scan
scripts and entropy-coding modes, verifies every candidate **pixel-identical**
to the source, and keeps the smallest verified result.

Typical savings (see [Benchmarks](#benchmarks)): ~5–8% with progressive
Huffman, ~11–14% when arithmetic coding is enabled.

## Features

- **Lossless by construction** — every candidate is decoded and compared
  pixel-for-pixel against the source before it is counted.
- **23 progressive-Huffman scan scripts** — including the mozjpeg *max
  compression* and *default* families (`src/transcoder.cpp`).
- **Guetzli candidate** — independent sequential encoder with cost-clustered
  Huffman tables.
- **Optional arithmetic coding** — 21 progressive + 1 sequential scripts
  (off by default; see [Arithmetic coding](#arithmetic-coding)).
- **Metadata stripping** — EXIF, XMP, ICC, IPTC, Adobe, JFIF/JFXX, comments.
- **Threshold mode** — keep the original unless savings reach N%.
- **Multi-threading** — worker pool with auto-detected core count.

## Build

Requires a C++17 compiler and CMake ≥ 3.20. libjpeg-turbo is built out-of-tree
by CMake; SIMD falls back to C intrinsics automatically (nasm is optional).

```sh
git clone <repo-url> jpegopt && cd jpegopt
cmake -S . -B build
cmake --build build -j
ctest --test-dir build            # 1/1: roundtrip + verification tests
```

No submodules and no downloads: libjpeg-turbo and guetzli are vendored in
`vendor/`. CI builds and tests on Ubuntu (see `.github/workflows/ci.yml`);
the code itself is platform-independent C++17.

## Usage

```
Usage: jpegopt [options] <file|dir> ...

  -t, --threads N     worker threads (default: auto)
  -i, --in-place      overwrite the source file with the best result
      --dry-run       report only, do not write anything
      --arith         also try arithmetic coding candidates
      --strip-metadata drop APP/COM markers (EXIF/JFIF/comments)
      --strip-exif    strip EXIF (APP1) markers only
      --strip-xmp     strip XMP (APP1) markers only
      --strip-icc     strip ICC color profile (APP2) markers
      --strip-iptc    strip IPTC/Photoshop (APP13) markers
      --strip-adobe   strip Adobe (APP14) markers
      --strip-jfif    strip JFIF (APP0) markers
      --strip-jfxx    strip JFXX (APP0 extension) markers
      --strip-com     strip comment (COM) markers
  -T, --threshold N   keep the original unless savings reach N%
      --files-from F  read the list of files to process from file F
      --files-stdin   read the list of files to process from stdin
  -p, --preserve      copy source timestamps/mode to new outputs
      --json          machine-readable JSON report
      --show-all      list every candidate size per file
      --temp-dir PATH directory for temporary files
  -v, --verbose       verbose output
  -V, --version       show version and exit
  -h, --help          show this help

Output (default): <name>.opt.jpg next to each source file.
```

Examples:

```sh
./build/jpegopt --dry-run photos/          # report only
./build/jpegopt -i photos/                 # in-place optimize
./build/jpegopt --arith -i --show-all photos/
./build/jpegopt --strip-exif -T 2 -i --files-stdin < list.txt
./build/jpegopt --json --show-all photos/  # machine-readable report
```

Marker stripping is applied per category and never touches pixels: every
result is still decoded and verified pixel-identical before it is written.
`--strip-metadata` removes everything; the `--strip-*` flags remove a single
category. In-place writes preserve the original file's timestamps
automatically; `-p` extends this to fresh `<name>.opt.jpg` outputs.

## How it works

1. The source is decoded once to a reference image (independent TurboJPEG
   decoder).
2. A pool of candidates is produced by *lossless transcode* (libjpeg-turbo's
   jpeglib API — no pixel is ever recompressed).
3. Every candidate is decoded and compared against the reference; only
   pixel-identical candidates count.
4. The smallest verified candidate wins; nothing is written unless a
   verified result beats the source.

Because everything is verified lossless, the output is always a perfectly
valid JPEG that decodes to the exact same pixels.

### Arithmetic coding

Arithmetic-coded JPEGs are **not** supported by Chrome, Firefox, Photoshop,
most phones and many libraries. `--arith` is off by default; pass it only
when you control the decoder on the other end (e.g. your own storage for a
self-managed archive) and want the extra ~5 percentage points.

## Benchmarks

Measured with `--dry-run --json --show-all` (best-of-N candidates, all
verified lossless; files with 0 error rate).

| corpus | files | huff only | +arith | best mode |
|---|---|---|---|---|
| mixed corpus (sample_11, 2 506 files) | 2 506 | 5.45% | 10.72% | `progressive-mozmax-al4` / `progressive+arith-al6-dcplain` |
| phone photos, 12 MP realme (144 files) | 144 | 7.38% | 14.12% | same modes, won 144/144 |

guetzli is included as a candidate but rarely wins (44/2 574 files in the
huff-only sweep; 0 in arith mode); it is kept because it is a fully
independent re-encode path with occasional wins on small images.

## Dependencies and licensing

- **Our code**: Apache-2.0 (`LICENSE`, SPDX headers in `src/`).
- **libjpeg-turbo** — vendored in-tree `vendor/libjpeg-turbo`
  (BSD 3-Clause), no local modifications.
- **guetzli** — vendored in-tree `vendor/guetzli` (Apache-2.0) with one
  local patch (see `vendor/guetzli/guetzli/JPEGOPT_PATCH.md`).
- **mozjpeg** — not vendored; the *mozmax / moz-default / moz-fast*
  progressive scan scripts in `src/transcoder.cpp` are adapted from
  mozjpeg's `jcparam.c` (BSD 3-Clause).

The `--strip-*`, `--threshold`, `--files-from/--files-stdin` and `-p` options
mirror familiar command-line conventions of
[jpegoptim](https://github.com/tjko/jpegoptim) (GPL-3.0); the implementation
here is original, independent code, licensed Apache-2.0.

Full attribution in `NOTICE`. See [CHANGELOG.md](CHANGELOG.md) for release
history and [CONTRIBUTING.md](CONTRIBUTING.md) for how to contribute.

## Acknowledgments

This project was written with the assistance of **Big Pickle**
([opencode/big-pickle](https://opencode.ai)), an AI coding assistant.