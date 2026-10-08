# Scan scripts

A progressive JPEG stores coefficients as a sequence of *scans*. Each scan
carries a band of coefficients (spectral range `Ss..Se`, component subset) and
optionally a successive-approximation step (`Ah`, `Al`). Because the entropy
coder is fed one scan at a time, the scan script changes how well the Huffman or
arithmetic coder compresses the same coefficients.

jpegopt therefore treats "which scan script?" as a search dimension: it builds
many candidates and keeps the smallest verified one.

The code lives in `src/transcoder.cpp` (`build_scan_script`); the candidate lists
are in `src/pipeline.cpp` (`huff_styles[]`, `arith_styles[]`).

## How many candidates

| Mode | Candidates | Composition |
|---|---|---|
| default (Huffman) | **24** | 23 progressive scan scripts + guetzli sequential |
| `--arith` | **45** | 24 above + 20 progressive arithmetic + 1 sequential arithmetic |

## Why exactly 23 and 20

The lists are not arbitrary. Every entry satisfies one rule:

> **A scan script is kept only if it won at least one file in a corpus-wide
> sweep.**

This is stated in the source comments (the `huff_styles[]` comment in `src/pipeline.cpp`,
the `arith_styles[]` comment in `src/pipeline.cpp`) and enforced by `scripts/analyze_bench.py`, whose
`KEEP`/`DROP` report drives the pruning. In other words the sets are the
survivors of measurement, not of guesswork: a script that never produced the
smallest verified result on the corpus was removed because it only cost time.

The sets differ between modes because the ranking differs: a script that never
won under Huffman coding may still win under arithmetic coding, and vice versa.
That is why, for example, `kSuperFineAl2` is arithmetic-only while `kFineBands`
is Huffman-only (the `H`/`A` columns below are the authoritative record).

## The scripts

`ScanStyle` is declared in `src/transcoder.h`. Legend: **H** = in the Huffman
candidate list, **A** = in the arithmetic candidate list, **—** = implemented but
not used by any candidate.

| ScanStyle | H | A | Structure |
|---|:---:|:---:|---|
| `kLibJpeg` | ● | ● | libjpeg-turbo `jpeg_simple_progression` |
| `kFewScan` | ● | ● | DC + one AC band (1-63) per component, Al=1 |
| `kFineBands` | ● | — | DC + AC bands 1-4 / 5-8 / 9-16 / 17-32 / 33-63, Al=1 |
| `kMediumBands` | — | — | DC + AC bands 1-8 / 9-16 / 17-31 / 32-63, Al=1 |
| `kSuperFine` | ● | — | DC + 7 fine AC bands, Al=1 |
| `kFewScanAl2` | — | ● | `kFewScan` with first-pass Al=2 |
| `kFewScanAl3` | ● | ● | `kFewScan` with first-pass Al=3 |
| `kFewScanAl4` | — | ● | `kFewScan` with first-pass Al=4 |
| `kMozMax` | ● | ● | mozjpeg max-compression, DC plain, luma Al=2 |
| `kMozMaxDcSa1` | ● | — | mozjpeg structure, DC successive approximation |
| `kMozMaxSplit5` | ● | — | mozjpeg structure, luma AC split 1-5 / 6-63 |
| `kMozMaxChromaSa` | ● | — | mozjpeg structure, chroma AC successive approximation |
| `kMozMaxAl3` | ● | ● | mozjpeg structure, luma first-pass Al=3 |
| `kMozMaxAl4` | ● | ● | mozjpeg structure, luma first-pass Al=4 |
| `kMozMaxSplit5Al3` | ● | ● | mozjpeg structure, luma split 1-5 and Al=3 |
| `kMozMaxAl3ChromaSa` | ● | ● | mozjpeg structure, luma Al=3 and chroma SA |
| `kFewScanAl1DcPlain` | ● | ● | DC in a single (0,0) scan, `kFewScan` AC |
| `kFewScanAl2DcPlain` | ● | ● | DC plain, `kFewScan` AC, Al=2 |
| `kFewScanAl3DcPlain` | ● | ● | DC plain, `kFewScan` AC, Al=3 |
| `kFewScanAl4DcPlain` | ● | ● | DC plain, `kFewScan` AC, Al=4 |
| `kFewScanAl5DcPlain` | ● | ● | DC plain, `kFewScan` AC, Al=5 |
| `kFewScanAl6DcPlain` | ● | ● | DC plain, `kFewScan` AC, Al=6 |
| `kFineBandsDcPlain` | ● | — | DC plain, fine AC bands |
| `kMediumBandsDcPlain` | ● | — | DC plain, medium AC bands |
| `kMediumBandsAl2` | — | ● | medium AC bands, Al=2 |
| `kMozDefault` | ● | ● | mozjpeg `JCP_DEFAULT` |
| `kMozFast` | ● | — | mozjpeg `JCP_FAST` |
| `kSuperFineAl2` | — | ● | super-fine AC bands, Al=2 |
| `kSuperFineAl2DcPlain` | — | — | super-fine AC bands, Al=2, DC plain |
| `kSuperFineAl3` | — | ● | super-fine AC bands, Al=3 |
| `kSuperFineAl3DcPlain` | — | — | super-fine AC bands, Al=3, DC plain |

31 styles are implemented, 28 are reachable as candidates. The three unused
ones (`kMediumBands`, `kSuperFineAl2DcPlain`, `kSuperFineAl3DcPlain`) lost the
corpus sweep and were left in the enum for reference and future measurement.

## Plain-DC versus successive-approximation DC

`DcPlain` scripts send the DC coefficients in a single `(Ah=0, Al=0)` scan and
apply successive approximation to the AC bands only. The alternative sends DC
in successive approximation as well. Both compress differently: which one wins
is image-dependent, which is why several members of each family are kept.

## mozjpeg lineage and licensing

The `kMozMax*`, `kMozDefault` and `kMozFast` families reimplement the scan
structures published in mozjpeg's `jcparam.c` (the `JCP_MAX_COMPRESSION`,
`JCP_DEFAULT` and `JCP_FAST` profiles). They are parameterised so the same
builder can emit several variants:

- `dc_sa ∈ {0,1}` — DC successive approximation on/off;
- `luma_split` — where the luma AC range is split (8 by default, 5 in the
  `Split5` variants);
- `luma_al` — the luma first-pass `Al` (2, 3 or 4);
- `chroma_sa ∈ {0,1}` — chroma AC successive approximation.

For 3-component YCbCr images the exact asymmetric mozjpeg scan list is
reproduced; for any other component count (grayscale, CMYK) a generic
all-component form is emitted instead — see
[limitations.md](limitations.md).

mozjpeg is **not** a build dependency and is not vendored. It is only the source
of these structures (BSD 3-Clause); the C++ implementation in
`src/transcoder.cpp` is ours. Attribution is in the root `NOTICE`.

## libjpeg validation constraints

Any generated script must satisfy libjpeg's own rules
(the comment above `build_scan_script` in `src/transcoder.cpp`), which shape the builder:

- AC scans must contain exactly **one** component; only DC scans may interleave
  components.
- The `Ah`/`Al` sequence of each coefficient must step down by one: first
  `Ah=0`, then `Ah` = previous `Al`, `Al` = `Ah-1`.

A script that violates these is rejected by libjpeg at `jpeg_write_coefficients`
time, so the builder constructs only valid sequences by construction rather
than validating afterwards.

## Adding a new scan script

1. Add the value to `enum class ScanStyle` in `src/transcoder.h` with a comment
   describing the structure.
2. Add a case to `build_scan_script` in `src/transcoder.cpp`.
3. Add the candidate tag to `huff_styles[]` and/or `arith_styles[]` in
   `src/pipeline.cpp` (the tag is the user-visible name reported by `--json`,
   `--show-all` and the text report).
4. Add the expected tag to the `required` list in
   `tests/roundtrip_test.cpp` — the suite fails if a declared candidate does not
   appear, which catches typos and broken builders.
5. Measure it on a corpus. If it never wins, do **not** keep it: remove it from
   the candidate lists (the enum entry may stay). This is the same rule that
   produced the current 23/20 sets.

## Source-script preservation

`TranscodeOptions::scan_override` can re-encode using the *source's own* scan
script, extracted by `extract_scan_script` (`src/jpeg_reader.cpp`). It is
implemented and covered by the test suite, but it is not currently wired to any
candidate, because on the reference corpora it never won.
