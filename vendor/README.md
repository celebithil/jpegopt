# Vendored code

Both third-party dependencies are committed into this repository so that a
clone is enough to build: **no git submodules, no downloads at configure or
build time**. This keeps CI and offline builds reproducible at the cost of a
larger tree.

| Component | Upstream | Version / commit | Licence | Local changes |
|---|---|---|---|---|
| `libjpeg-turbo/` | https://github.com/libjpeg-turbo/libjpeg-turbo | `f29eda6` (3.0.4) | BSD 3-Clause | none |
| `guetzli/` | https://github.com/google/guetzli | `214f2bb` | Apache-2.0 | one patch (below) |

Attribution for both, plus mozjpeg-derived scan scripts, is in the root
[`NOTICE`](../NOTICE).

## libjpeg-turbo

Used for two distinct APIs from a single archive:

- **jpeglib** (`transcoder.cpp`) — lossless transcoding of coefficient arrays,
  marker copying, scan-script control, arithmetic coding.
- **TurboJPEG** (`verify.cpp`) — the independent decoder used to verify that
  every candidate is pixel-identical to the source.

The build enables `WITH_TURBOJPEG=ON` and `ENABLE_SHARED=OFF` precisely so that
one `turbojpeg-static` archive provides both, and disables Java, tests, fuzzers
and 12-bit support. It is compiled out-of-tree by CMake as an `ExternalProject`
into `build/turbo/`; see [`docs/build.md`](../docs/build.md).

**The copy is verbatim upstream.** Do not edit it.

## guetzli

Guetzli contributes a genuinely different re-encode path: its own JPEG reader
and writer with cost-clustered Huffman tables, which sometimes beats libjpeg by a
margin worth having. It is used as one candidate in the default search.

### What is compiled

Only the self-contained entropy-coding files, listed in the root
`CMakeLists.txt`:

```
guetzli/entropy_encode.cc
guetzli/jpeg_data.cc
guetzli/jpeg_data_reader.cc
guetzli/jpeg_data_writer.cc
guetzli/jpeg_huffman_decode.cc
```

Everything else in the tree — butteraugli, libpng stubs, `.vcxproj` and
`snapcraft` files, test images — is **present for context but not built**. Do
not assume those files are maintained or consistent with our build.

### The one local patch

`guetzli/jpeg_data_reader.cc` adds a no-op `fprintf` near the top of the file.
Guetzli's parser prints diagnostics to stderr; jpegopt runs many decoders
concurrently on worker threads, so those writes would interleave and corrupt the
report on stdout/stderr. The patch suppresses them.

Nothing else differs from upstream. The patch is documented in
[`guetzli/guetzli/JPEGOPT_PATCH.md`](guetzli/guetzli/JPEGOPT_PATCH.md).

Note the nested path: the sources live at `guetzli/guetzli/*.cc`, i.e. two
levels below this directory.

## Updating a dependency

1. Note the current commit (table above, and `NOTICE`).
2. Replace the tree with the new upstream revision.
3. For guetzli, re-apply the `fprintf` no-op and update
   `JPEGOPT_PATCH.md` if it changed.
4. Confirm the candidate list still builds and passes: `cmake -S . -B build &&
   cmake --build build -j && ctest --test-dir build`.
5. Re-run the corpus bench (`docs/benchmarks.md`) — a new libjpeg-turbo can change
   the winner distribution and therefore justify re-tuning the candidate lists.
6. Update this file and `NOTICE` with the new commit and licence.

## Rules

- Vendored code is upstream code. Project logic belongs in `src/`.
- Any local modification must be minimal, commented in place, documented in
  `JPEGOPT_PATCH.md`, and reflected in `NOTICE`.
- Never mix formatting or unrelated edits into a vendored tree; it makes future
  updates painful.
