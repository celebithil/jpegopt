# Architecture

Audience: developers. This document describes the data flow and the invariants
that must not be broken.

## Module map

| File | Responsibility |
|---|---|
| `src/main.cpp` | Argument handling entry, thread pool, report emission, exit code |
| `src/cli.{h,cpp}` | Option parsing, `--help` text, input expansion (files/directories) |
| `src/pipeline.{h,cpp}` | Per-file orchestration: reference decode, candidate generation, verification, selection, writing |
| `src/transcoder.{h,cpp}` | Lossless transcode via libjpeg-turbo; scan-script builder; marker copy; restart stripping |
| `src/guetzli_encode.{h,cpp}` | Independent re-encode path through guetzli's reader/writer |
| `src/verify.{h,cpp}` | Independent decode (TurboJPEG) and pixel comparison |
| `src/jpeg_reader.{h,cpp}` | Header parsing, marker classification, scan-script extraction, marker stripping |
| `src/report.{h,cpp}` | Human-readable and JSON reports |
| `src/util.{h,cpp}` | File I/O, atomic write with metadata preservation, temp dirs, formatting |

## Per-file flow

```
process_file(path)
  │
  ├─ read_file()                       whole file into memory
  ├─ read_jpeg_info()                  parse SOF: size, progressive?, arithmetic?, DRI
  │                                     → invalid input aborts early
  ├─ decode_rgb(original)              TurboJPEG → reference RGB
  │                                     → this is the verification baseline
  ├─ make_temp_dir()                   created and removed; candidates are in-memory
  │
  ├─ for each progressive scan script (23)     lossless_transcode() → candidate
  ├─ guetzli_encode()                          → candidate
  │  └─ if --arith:
  │     ├─ sequential arithmetic transcode     → candidate
  │     └─ 20 progressive arithmetic scripts   → candidates
  │
  │   each candidate, before verification:
  │     └─ maybe_strip()             granular marker policy applied to the bytes
  │
  ├─ add_if_valid()                    decode candidate → compare to reference
  │                                     failure ⇒ silently discarded
  │
  ├─ classify_strippable(original)    what the policy would remove (reporting)
  │
  ├─ select winner                    arithmetic only if strictly smaller
  ├─ threshold check                  savings_pct ≥ min_savings_pct ?
  └─ write_atomic()                   unless --dry-run
```

## Invariants

**1. Nothing unverified is ever counted or written.**
`add_if_valid` (`add_if_valid` in `src/pipeline.cpp`) is the only path by which a candidate
enters the pool, and it decodes the candidate and compares it to the reference
before accepting. A failing candidate leaves no trace — not in `candidates`, not
in the text report, not on disk.

**2. The reference decode is independent of the encoder.**
The baseline is produced by TurboJPEG (`src/verify.cpp`), while candidates are
produced by libjpeg-turbo's jpeglib API and by guetzli. Using the same library
for both sides would let a shared bug cancel out, so the decoder is deliberately
a different code path.

**3. No pixel is ever recompressed.**
Candidates are produced by *transcoding coefficient arrays*
(`jpeg_read_coefficients` → `jpeg_write_coefficients`), never by decoding to
pixels and re-encoding. Quantization tables are copied verbatim from the source;
only scan structure, entropy coding and marker handling change.

**4. Arithmetic results are used only when strictly smaller.**
Even under `--arith`, an arithmetic candidate wins only if it beats the best
Huffman candidate byte-for-byte (the winner-selection block in `process_file` (`src/pipeline.cpp`)). Otherwise the
Huffman result is used and `arith_fell_back` is set. This bounds the "surprise
factor" of the flag.

**5. The output never grows.**
A candidate must be strictly smaller than the source to be written. Combined
with invariant 1, the tool is safe to run over a whole tree in place.

## Threading model

`src/main.cpp` starts a fixed pool of workers that pull file indices from an
atomic counter; results are stored at their input index, so report order is
deterministic regardless of scheduling.

Default pool size is `cores / 4`, clamped to `[1, 4]`
(the thread-pool sizing block in `src/main.cpp`) — the per-file workload is large, so oversubscribing
cores costs more than it gains. `-t N` overrides, subject to a ceiling of 64
workers and a non-negative-integer check (`src/cli.cpp`); see
[limitations.md](limitations.md) for the memory reasoning.

libjpeg error handling uses `setjmp`/`longjmp` across the C boundary, with the
`jpeg_error_mgr` state in `thread_local` storage
(the `thread_local` error state in `src/transcoder.cpp`) so writes between `setjmp` and `longjmp` remain
well-defined.

## Why guetzli is a candidate

The other candidates all go through libjpeg-turbo. guetzli is a genuinely
different reader/writer pair with cost-clustered Huffman tables, so it can win
where libjpeg loses. Two constraints apply:

- guetzli's reader only understands Huffman-coded SOF markers, so arithmetic
  sources are rejected up front and the candidate is simply absent
  (the arithmetic check in `guetzli_encode`);
- the vendored copy carries a one-line patch that no-ops `fprintf` so that
  concurrent decoders do not interleave on stdout/stderr
  (see `vendor/guetzli/guetzli/JPEGOPT_PATCH.md`).

## Marker handling: two mechanisms

`--strip-metadata` uses the **fast path**: markers are never copied by the
encoder (`JCOPYOPT_NONE` in the `jcopy_markers_*` calls in `src/transcoder.cpp`).

The granular `--strip-*` flags use the **post-encode path**: the candidate is
encoded with all markers, then `strip_markers` rewrites the header region
(`src/jpeg_reader.cpp`). This is what allows EXIF and XMP — both APP1 — to be
distinguished by payload signature. Both paths run before verification, so
stripping can never invalidate a candidate.

## Dependency direction

```
main ──> cli, pipeline, report
pipeline ──> transcoder, guetzli_encode, verify, jpeg_reader, util
transcoder ──> libjpeg-turbo (jpeglib, transupp)
verify ──────> libjpeg-turbo (turbojpeg)
guetzli_encode ──> vendored guetzli entropy-coding subset
```

`pipeline.cpp` is the only module that knows the full candidate list. Adding a
new re-encode path means adding a module here and wiring one call.
