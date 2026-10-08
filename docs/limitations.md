# Known limitations and caveats

Everything here is deliberate or known-and-accepted. None of it is a bug, but
several items are trade-offs you should know about before running the tool over
important data.

## Restart markers are always removed

Every progressive candidate and the sequential arithmetic candidate are encoded
with `restart_interval = 0` and `restart_in_rows = 0`
(the `strip_restart` branch in `lossless_transcode`, enabled at where `TranscodeOptions::strip_restart` is set in `src/pipeline.cpp`).

- **Why:** restart markers (DRI/RSTn) cost bytes and contribute nothing to
  compression.
- **Trade-off:** they help decoders resynchronize after a truncated or corrupt
  stream. A JPEG with restart markers is more robust to partial reads; the
  output is not. This is the one change jpegopt makes that is lossless for
  pixels but not byte-structure-preserving.
- **Not configurable.** There is no flag to keep them.

## Progressive input may become sequential

The candidate set is not restricted to progressive output. The guetzli candidate
is a **sequential** encoder, and the sequential arithmetic candidate is
sequential too. Either can win on size, so a progressive source may be written
out as a sequential JPEG.

Pixels are unchanged and the result is still verified; what is lost is
progressive rendering. If you need progressive output guaranteed, do not use
`--arith` blindly — check the reported `method`, or restrict yourself to a
pipeline where the guetzli candidate is acceptable.

## Arithmetic-coded input disables the guetzli candidate

guetzli's reader only understands Huffman-coded SOF markers. Arithmetic sources
are rejected up front (the arithmetic check in `guetzli_encode`), so for such files the
guetzli candidate is simply absent from `candidates`. The transcode candidates
still work.

## Arithmetic coding is off by default

Arithmetic-coded JPEGs are not supported by Chrome, Firefox, Photoshop, most
phones and many imaging libraries. `--arith` adds roughly five percentage
points; enable it only when the consumer of the file is under your control.

## Non-3-component images get a generic scan script

The mozjpeg-structure families (`kMozMax*`, `kMozDefault`, `kMozFast`) reproduce
mozjpeg's exact asymmetric scan list only for 3-component YCbCr images. For
grayscale or CMYK sources a generic all-component form is emitted instead
(the `ncomps != 3` branch of `add_mozmax_script`). Such images therefore compress slightly worse
under those candidates. Nothing fails; the results are still valid and verified.

## 12-bit JPEG: supported, but only one candidate

12-bit-per-sample sources **are** optimized. libjpeg-turbo 3.0.x ships 12-bit
support unconditionally (there is no `WITH_12BIT` option; an earlier
`-DWITH_12BIT=OFF` in this repo was silently ignored), and the pipeline handles
them through a separate route.

The vendored build has no 12-bit *coefficient* API (`jpeg12_read_coefficients`
is not compiled), so 12-bit sources cannot go through the jpeglib transcode path
that produces the 23 progressive scan-script candidates. Instead they are
re-encoded with TurboJPEG's `tj3Transform` (progressive + optimized Huffman,
coefficients untouched), reported as the single candidate
`progressive-12bit`. Verification decodes both sides at 12-bit precision and
compares sample-for-sample, so the result is still provably lossless.

Practical consequences for a 12-bit source:

- exactly **one** candidate is produced, not 24/45;
- `--arith` is ignored (arithmetic coding is unavailable on this route);
- the savings are correspondingly smaller than for 8-bit camera JPEGs;
- guetzli is not attempted (its reader only understands 8-bit Huffman input).

16-bit-per-sample JPEG is rejected.

## Colour models

Grayscale, RGB/YCbCr, RGB and CMYK sources are all optimized losslessly. The
verification decodes a candidate back into the **source's own** colour model and
bit depth — CMYK stays 4-channel, grayscale stays single-channel — rather than
converting to RGB. Comparing in the source's model is what makes "pixel
identical" a meaningful claim: a conversion would hide differences the encoder
could legitimately introduce.

Earlier versions decoded everything to RGB, which made CMYK sources fail with
`tjDecompress2: Unsupported color conversion request`; such files are now
processed normally.

## POSIX/Linux only

The code uses `fcntl.h`, `sys/stat.h`, `unistd.h`, `getpid`, `chmod`, `rename`
and `utimensat` (`src/util.cpp`). It builds and
runs on Linux and other POSIX hosts; Windows and non-Linux POSIX systems
(`/proc` absent) will not build or run as is. CI covers Ubuntu only.

## Temporary files are created next to the output

Each published result is written to a temporary file and then moved into place,
so an interrupted run cannot leave a half-written output. By default the
temporary file lives in the destination directory (making the final step a single
atomic `rename`); `--temp-dir` stages it elsewhere instead, which is useful when
the destination filesystem is slow or has tight free space. When the staging
directory is on a different filesystem the bytes are copied to a temporary file
beside the target and renamed from there, so the visible result still appears
atomically.

Temp file names are `.jpegopt.tmp.<pid>.<seq>` with a per-process counter, so
two outputs in the same directory cannot collide. Leftovers from a killed run are
safe to delete; they are ignored by directory scans (they have no `.jpg`
extension).

## Non-existent inputs are skipped silently

An input path that does not exist is ignored rather than reported
(`collect_inputs` in `src/cli.cpp`). A directory containing no JPEG files yields "no JPEG
files found" and exit code 2, but a mistyped single filename produces no
diagnostic beyond its absence from the report. In scripts, compare the number of
reported results against the number of files you expected.

## The verification decoder is a reference, not a proof

Pixel identity is established by decoding the source and the candidate with
TurboJPEG and comparing RGB buffers. This is strong evidence — the encoders
under test are different code paths — but it is a decoder-based check. Exotic
files (malformed markers, unusual component counts) are handled by rejecting
them rather than by special-casing; such files are skipped or reported as
errors, never silently "optimized".

## Thread pool defaults are conservative

Default workers = `cores / 4`, clamped to `[1, 4]`. On a 64-core machine
jpegopt uses 4 threads by default. This is intentional (large per-file buffers),
but it means `-t` is worth setting on big corpora.

`-t N` requires a non-negative integer (`abc`, `2x`, `-3` are rejected with exit
2) and is clamped to a hard ceiling of 64 workers. Each worker holds a decoded
reference frame plus the candidate set for its file, so memory scales with the
worker count: on 12 MP photos, roughly 1 GB resident at `-t 4` and ~3–4 GB at
the default-uncapped sizes seen before the cap. The ceiling exists so a typo like
`-t 1000000` cannot exhaust the machine.

## In-place writes replace the file's inode

Every published result — including `-i/--in-place` — is written to a temporary
file and atomically renamed over the destination. Two consequences are worth
knowing, and neither is a bug:

- **A read-only source is still replaced.** The rename does not need write
  permission on the file, only on its directory. The original mode is copied to
  the replacement, so a `444` file stays `444` — but its contents changed.
- **Hardlinks are broken.** After an in-place run the source path points at a new
  inode, so any other name hardlinked to it keeps the old contents. Copy instead
  of hardlink if you rely on that linkage.

The same applies to fresh `<name>.opt.<ext>` outputs, except that the original is
left untouched, so only the "new inode" aspect applies.
