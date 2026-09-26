# Comparison

How jpegopt differs from the tools it overlaps with. Short version: the common
lossless tools each re-encode with *one* strategy and trust the result; jpegopt
searches a family of strategies, verifies every one against an independent
decoder, and keeps the smallest.

## Feature matrix

| | jpegopt | jpegoptim | mozjpeg (`cjpeg -progressive`) | `jpegtran -optimize` | guetzli | ECT |
|---|---|---|---|---|---|---|
| Lossless | yes (always) | yes | yes | yes | no (lossy) | no (lossy) |
| Candidates per file | **24 (or 45 with `--arith`)** | 1–2 | 1 | 1 | 1 | 1 |
| Verifies pixels before writing | **yes, every candidate** | no | no | no | no | no |
| Arithmetic coding | opt-in (`--arith`) | build-time opt | no | yes (`-arithmetic`) | no | no |
| Chooses smallest automatically | **yes** | yes (best of its own) | no | no | no | no |
| Granular metadata stripping | **8 categories** | 9 keep/strip flags | via `jpegtran` | all/none | no | no |
| Threshold ("keep unless N% saved") | **yes** | yes (`-T`) | no | no | no | no |
| File-list input | **yes** | yes | no | no | no | no |
| Preserves timestamps | **yes** | opt-in | no | no | no | no |
| Machine-readable report | **JSON** | CSV-ish | no | no | no | no |
| Native C++ | **yes** | C | C | C | C++ | mixed |
| Licence | Apache-2.0 | GPL-3.0 | BSD/ISC | IJG | Apache-2.0 | mixed |

## Trade-offs

**jpegopt is slower per file.** Building 24 (or 45) candidates and decoding each
for verification is far more work than one re-encode. In exchange you get the
best of that family, and a guarantee. For a one-off file this is usually a bad
trade; for a corpus, running the search once and keeping the result is the point.

**The gain over a single good tool is modest but real.** On the reference
corpora, mozjpeg-style progressive with optimized tables is the strong
baseline; jpegopt's scan-script search plus arithmetic coding is what produces
the extra few percent. A tool that already tries several scripts (some
`jpegoptim` builds, ImageOptim-style orchestrators) closes part of the gap but
still does not verify pixels.

**Verification costs a decode.** Every candidate is decoded to RGB and compared.
That is the price of the "lossless by construction" claim; it also means
incompatible inputs (CMYK, 12-bit, arithmetic sources for guetzli) degrade
gracefully — the candidate is dropped, not written.

**No lossy mode.** jpegopt is deliberately lossless-only. jpegoptim (`-m`,
`-S`), guetzli, ECT and ImageOptim's "lossy" mode trade pixels for size; if you
want that, use them (or a dedicated tool) for it. jpegopt complements them by
squeezing the lossless ceiling.

## When to use which

- **Max lossless size reduction, pixels must be identical** → jpegopt.
- **A single fast pass over many files, metadata stripping** → jpegoptim
  (`-o --all-progressive`) is fine.
- **Maximum compression at a given quality (lossy)** → mozjpeg `cjpeg
  -progressive`, `guetzli`, or ECT; consider jpegopt's arithmetic mode first
  if your decoder tolerates it.
- **Storage where you control the reader and want lossless ceiling** → jpegopt
  with `--arith`.

## Provenance

The `--strip-*`, `--threshold`, `--files-from/--files-stdin` and `-p` options
mirror command-line conventions established by
[jpegoptim](https://github.com/tjko/jpegoptim) (GPL-3.0). The implementation in
this project is original, independent code under Apache-2.0; no GPL code was
copied.

The mozmax / moz-default / moz-fast progressive scan structures in
`src/transcoder.cpp` are reimplementations of the profiles published in
mozjpeg's `jcparam.c` (BSD 3-Clause); mozjpeg itself is not a dependency.
