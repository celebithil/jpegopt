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

## 12-bit JPEG is not supported

The vendored libjpeg-turbo is configured with `WITH_12BIT=OFF`
(`CMakeLists.txt:43`), so 12-bit-per-sample files are rejected. This is not a
lossless-optimization decision; the build simply does not include 12-bit
support.

## POSIX/Linux only

The code uses `fcntl.h`, `sys/stat.h`, `unistd.h`, `getpid`, `chmod`, `rename`,
`utimensat` and `readlink("/proc/self/exe")` (`src/util.cpp`). It builds and
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
