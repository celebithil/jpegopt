# jpegopt

Lossless JPEG optimizer. Re-encodes JPEGs with a battery of scan scripts and
entropy-coding modes, verifies every candidate **pixel-identical** to the
source, and keeps the smallest verified result.

Typical savings (see [Benchmarks](#benchmarks)): ~5–8% with progressive
Huffman, ~11–14% when arithmetic coding is enabled.

## How it works

1. The source is decoded once to a reference image (independent TurboJPEG
   decoder).
2. A pool of candidates is produced by *lossless transcode* (libjpeg-turbo's
   jpeglib API — no pixel is ever recompressed):
   - 23 progressive-Huffman scan scripts (including the mozjpeg *max
     compression* and *default* families, `src/transcoder.cpp`),
   - the guetzli sequential encoder with cost-clustered Huffman tables
     (`third_party/guetzli`, vendored in-tree, see `JPEGOPT_PATCH.md`),
   - optionally 21 progressive + 1 sequential arithmetic-coding scripts.
3. Every candidate is decoded and compared against the reference; only
   pixel-identical candidates count.
4. The smallest verified candidate wins; nothing is written unless a
   verified result beats the source.

Because everything is verified lossless, the output is always a perfectly
valid JPEG that decodes to the exact same pixels.

## Build

```sh
git clone <repo> jpegopt && cd jpegopt
git submodule update --init        # fetches libjpeg-turbo (only dep)
cmake -S . -B build
cmake --build build -j
ctest --test-dir build             # 1/1: roundtrip + verification tests
```

Requires a C++17 compiler and CMake ≥ 3.20. libjpeg-turbo is built
out-of-tree by CMake (SIMD fallback to C intrinsics is automatic, no nasm
required). guetzli is compiled from the in-tree subset.

## Usage

```
Usage: jpegopt [options] <file|dir> ...

  -t, --threads N     worker threads (default: auto)
  -i, --in-place      overwrite the source file with the best result
      --dry-run       report only, do not write anything
      --arith         also try arithmetic coding candidates
      --strip-metadata drop APP/COM markers (EXIF/JFIF/comments)
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
```

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

guetzli is included as a candidate but rarely wins (44/27 574 files in the
huff-only sweep; 0 in arith mode); it is kept because it is a fully
independent re-encode path with occasional wins on small images.

## Dependencies and licensing

- **Our code**: Apache-2.0 (`LICENSE`, SPDX headers in `src/`).
- **libjpeg-turbo** — git submodule `third_party/libjpeg-turbo` (BSD 3-Clause);
  the only third-party dependency, no local modifications.
- **guetzli** — vendored in-tree `third_party/guetzli` (Apache-2.0) with one
  local patch (see `third_party/guetzli/JPEGOPT_PATCH.md`).
- **mozjpeg** — not vendored; the *mozmax / moz-default / moz-fast*
  progressive scan scripts in `src/transcoder.cpp` are adapted from
  mozjpeg's `jcparam.c` (BSD 3-Clause).

Full attribution in `NOTICE`.
