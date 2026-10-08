# jpegopt

[![CI](https://github.com/celebithil/jpegopt/actions/workflows/ci.yml/badge.svg)](https://github.com/celebithil/jpegopt/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](CMakeLists.txt)

Lossless JPEG optimizer. Re-encodes a JPEG with a battery of progressive scan
scripts and entropy-coding modes, verifies every candidate is **pixel-identical**
to the source, and keeps the smallest verified result.

Typical savings: **~5–8%** with progressive Huffman, **~11–14%** with arithmetic
coding enabled (see [Benchmarks](docs/benchmarks.md)).

## Features

- **Lossless by construction** — every candidate is decoded and compared
  pixel-for-pixel against the source before it is counted. Comparison happens in
  the source's **own colour model and bit depth** (grayscale, RGB, CMYK, or
  12-bit), so a candidate that fails verification is silently discarded without
  a lossy conversion hiding the difference.
- **23 progressive-Huffman scan scripts** — including the mozjpeg *max
  compression*, *default* and *fast* families
  ([docs/scan-scripts.md](docs/scan-scripts.md)).
- **Guetzli candidate** — an independent sequential encoder with cost-clustered
  Huffman tables.
- **Optional arithmetic coding** — 20 progressive + 1 sequential scripts
  (off by default; see [Arithmetic coding](#arithmetic-coding)).
- **Grayscale, RGB and CMYK sources** — all optimized in their own colour model.
- **12-bit sources** — optimized losslessly through TurboJPEG's transform path
  (one candidate, precision preserved; see
  [docs/limitations.md](docs/limitations.md)).
- **Granular metadata stripping** — EXIF, XMP, ICC, IPTC, Adobe, JFIF/JFXX,
  comments, or all of them ([docs/metadata-stripping.md](docs/metadata-stripping.md)).
- **Threshold mode** — keep the original unless savings reach N%.
- **JSON report** — machine-readable, scriptable ([docs/json-output.md](docs/json-output.md)).
- **Multi-threading** — worker pool, `cores/4` threads by default (1–4).

## Build

Requires a C++17 compiler, CMake ≥ 3.20 and a **POSIX/Linux** host.

```sh
git clone https://github.com/celebithil/jpegopt.git && cd jpegopt
cmake -S . -B build
cmake --build build -j
ctest --test-dir build            # 1/1: roundtrip + verification suite
```

No submodules and no downloads: libjpeg-turbo and guetzli are vendored under
`vendor/`. The first build is slow because libjpeg-turbo is built out-of-tree by
CMake. SIMD falls back to C intrinsics automatically, so nasm is optional.

More, including CMake options and debug builds: [docs/build.md](docs/build.md).

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
```

`jpegopt --help` is the authoritative source for this list; the block above is
a copy for convenience. Per-flag reference, return codes and interactions:
[docs/cli.md](docs/cli.md).

Examples:

```sh
jpegopt --dry-run photos/                 # report only, write nothing
jpegopt -i photos/                        # in-place optimize
jpegopt --arith -i --show-all photos/     # + arithmetic candidates
jpegopt --strip-exif -T 2 -i photos/      # strip EXIF, require >=2% gain
find photos -name '*.jpg' | jpegopt -i --files-stdin
jpegopt --json photos/ > report.json      # machine-readable report
```

By default each source produces `<name>.opt.<ext>` next to it, with the original
extension preserved (`photo.JPEG` → `photo.opt.JPEG`). In-place writes preserve
the original file's timestamps and mode; `-p` extends the same behaviour to
freshly created outputs.

Marker stripping never touches pixels: the result is still decoded and verified
pixel-identical before it is written. `--strip-metadata` removes every APP/COM
marker; the `--strip-*` flags remove one category each. See
[docs/metadata-stripping.md](docs/metadata-stripping.md) for the exact marker
signature table.

## How it works

1. The source is decoded once into a reference image by an independent
   TurboJPEG decoder.
2. Candidates are produced by **lossless transcoding** (libjpeg-turbo's jpeglib
   API — no pixel is ever recompressed) and by the guetzli re-encoder.
3. Every candidate is decoded and compared against the reference; only
   pixel-identical candidates count.
4. Arithmetic candidates are used only when strictly smaller than the best
   Huffman candidate.
5. The smallest verified candidate wins; nothing is written unless a verified
   result beats the source, and `--threshold` is satisfied.

Because everything is verified lossless, the output is always a valid JPEG that
decodes to exactly the same pixels. Note that restart-interval (DRI/RSTn) markers
are always removed — a lossless but non-byte-identical change that slightly
reduces robustness to truncated streams. Details and other caveats:
[docs/limitations.md](docs/limitations.md).

Pipeline internals: [docs/architecture.md](docs/architecture.md).

### Arithmetic coding

Arithmetic-coded JPEGs are **not** supported by Chrome, Firefox, Photoshop, most
phones and many libraries. `--arith` is off by default; pass it only when you
control the decoder on the other end (e.g. your own storage for a self-managed
archive) and want the extra ~5 percentage points.

## Benchmarks

Best-of-N over all candidates, every one verified lossless, 0 errors. Artifacts
and the full per-method breakdown: [`bench/summary.md`](bench/summary.md).

| corpus | files | Huffman only | + arithmetic | best Huffman | best arithmetic |
|---|---|---|---|---|---|
| mixed (COCO 2017 + Open Images v7) | 3 954 | **5.56%** | **10.90%** | `progressive-mozmax-al4` (1 167 wins) | `progressive+arith-al6-dcplain` (1 635 wins) |
| phone photos, 12 MP realme | 144 | **7.38%** | **14.12%** | `progressive-mozmax-al4` (144/144) | `progressive+arith-al6-dcplain` (144/144) |

On the phone-photo corpus a single candidate won all 144 files in both modes.
Camera JPEGs (baseline, lightly entropy-coded) leave more headroom than web
images, which are usually already well optimised — hence the higher percentages.

guetzli won 5 of 3 954 files in the mixed Huffman run and 0 in the arithmetic
run. It is kept because it is a fully independent re-encode path with occasional
wins on small images, not because it moves the average.

The measurement corpora are **not** redistributed with this project. Method,
corpus composition and exact commands: [docs/benchmarks.md](docs/benchmarks.md).

## Documentation

| Document | Audience | Contents |
|---|---|---|
| [docs/architecture.md](docs/architecture.md) | developers | pipeline data flow, invariants, module map |
| [docs/scan-scripts.md](docs/scan-scripts.md) | developers | every scan script, its parameters and why it is kept |
| [docs/build.md](docs/build.md) | developers | requirements, CMake options, vendored build, debug |
| [docs/cli.md](docs/cli.md) | users | every flag, defaults, interactions, exit codes |
| [docs/json-output.md](docs/json-output.md) | integrators | `--json` schema and semantics |
| [docs/metadata-stripping.md](docs/metadata-stripping.md) | users | marker signature table, stripping mechanics |
| [docs/benchmarks.md](docs/benchmarks.md) | everyone | methodology, corpora, reproduction |
| [docs/limitations.md](docs/limitations.md) | everyone | known limitations and caveats |
| [docs/testing.md](docs/testing.md) | contributors | what the test suite covers and what it does not |
| [docs/comparison.md](docs/comparison.md) | everyone | jpegopt vs jpegoptim, mozjpeg, guetzli, ECT, ImageOptim |
| [bench/summary.md](bench/summary.md) | everyone | measured results, artifacts, corpus composition |
| [vendor/README.md](vendor/README.md) | maintainers | vendored code, versions, patches, updates |

## Dependencies and licensing

- **Our code**: Apache-2.0 — see [LICENSE](LICENSE) and the SPDX headers in
  `src/`.
- **libjpeg-turbo** — vendored in-tree under `vendor/libjpeg-turbo`
  (BSD 3-Clause), no local modifications.
- **guetzli** — vendored in-tree under `vendor/guetzli` (Apache-2.0) with one
  local patch (see [vendor/guetzli/guetzli/JPEGOPT_PATCH.md](vendor/guetzli/guetzli/JPEGOPT_PATCH.md)).
- **mozjpeg** — *not* a build dependency and not vendored. The mozmax /
  moz-default / moz-fast progressive scan scripts in `src/transcoder.cpp` are
  reimplementations of the structures in mozjpeg's `jcparam.c` (BSD 3-Clause).

The `--strip-*`, `--threshold`, `--files-from/--files-stdin` and `-p` options
mirror familiar command-line conventions of
[jpegoptim](https://github.com/tjko/jpegoptim) (GPL-3.0); the implementation
here is original, independent code, licensed Apache-2.0.

Full attribution in [NOTICE](NOTICE). See [CHANGELOG.md](CHANGELOG.md) for
release history, [CONTRIBUTING.md](CONTRIBUTING.md) for how to contribute, and
[SECURITY.md](SECURITY.md) for reporting security issues.

## Acknowledgments

This project was written with the assistance of **Big Pickle**
([opencode/big-pickle](https://opencode.ai)), an AI coding assistant.
