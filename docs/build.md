# Building

## Requirements

| Requirement | Notes |
|---|---|
| C++17 compiler | GCC or Clang. C++17 is enforced (`CMAKE_CXX_STANDARD_REQUIRED`). |
| CMake ≥ 3.20 | |
| POSIX/Linux host | `src/util.cpp` uses `fcntl.h`, `sys/stat.h`, `unistd.h`, `utimensat`, `chmod` and `getpid`. |
| nasm | **Optional.** The vendored libjpeg-turbo is configured with `WITH_SIMD=ON REQUIRE_SIMD=OFF`, so when nasm is absent the build silently falls back to C intrinsics. CI installs nasm to exercise the SIMD path. |
| Network | Not needed. Both dependencies are vendored. |

## Quick start

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The first build is noticeably slower than later ones: libjpeg-turbo is built
out-of-tree by CMake as an `ExternalProject` during the parent build.

## CMake options

| Option | Default | Effect |
|---|---|---|
| `JPEGOPT_BUILD_TESTS` | `ON` | Build `roundtrip_test` and register the `roundtrip` CTest target. Set `OFF` for a slim build. |
| `CMAKE_BUILD_TYPE` | `Release` | Applied automatically if not set. |
| `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` | empty | Forwarded to the vendored libjpeg-turbo `ExternalProject`, so a sanitizer build instruments the decoder too (see below). |

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DJPEGOPT_BUILD_TESTS=ON
```

### Sanitizer builds

`CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` are passed through to the vendored
libjpeg-turbo, so the same command that instruments `src/` also instruments the
decoder that parses untrusted input:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
```

Expect the sanitizer run to be roughly an order of magnitude slower than a
release build.

## Vendored dependencies

Both are committed under `vendor/`; nothing is downloaded and there are no git
submodules. Details, versions and the local patch: [vendor/README.md](../vendor/README.md).

**libjpeg-turbo** is built out-of-tree via `ExternalProject_Add` and imported as
the static target `turbojpeg_static` (`CMakeLists.txt:20-53`). The configuration
deliberately enables `WITH_TURBOJPEG=ON` and `ENABLE_SHARED=OFF` so that a single
archive provides *both* the TurboJPEG API and the jpeglib API: the former is
used for independent pixel verification (`src/verify.cpp`), the latter for
lossless transcoding and marker copying (`src/transcoder.cpp`). Turning off
TurboJPEG would split that into two libraries for no benefit.

libjpeg-turbo 3.0.x includes 12-bit support unconditionally — there is no
`WITH_12BIT` option — and this build uses it: 12-bit sources are optimized via
TurboJPEG's transform path. What that route can and cannot do is described in
[limitations.md](limitations.md).

**guetzli** is compiled from a minimal in-tree subset: only the self-contained
entropy-coding files
(`entropy_encode.cc`, `jpeg_data.cc`, `jpeg_data_reader.cc`,
`jpeg_data_writer.cc`, `jpeg_huffman_decode.cc`). Butteraugli, libpng and the
build-system files are present in the tree for context but are not compiled.
The copy carries one local patch (silencing `fprintf` for thread safety).

## Targets

| Target | Kind | Purpose |
|---|---|---|
| `jpegopt` | executable | The CLI tool. |
| `jpegopt_core` | static library | All non-`main` code; shared with the tests. |
| `guetzli_enc` | static library | The guetzli subset. |
| `turbojpeg_static` | imported static | libjpeg-turbo, built by the external project. |
| `roundtrip_test` | executable | The test suite (when `JPEGOPT_BUILD_TESTS=ON`). |

## Tests

```sh
ctest --test-dir build --output-on-failure
```

One test target, `roundtrip`, covers every scan script, guetzli, arithmetic mode,
restart-marker removal, metadata handling, the threshold, the JSON report schema,
the CLI exit codes, the file-list flags, CMYK sources and edge-case inputs.
What it does and does not cover: [testing.md](testing.md).

There is also a regression benchmark that runs over a directory of real photos
when one is present:

```sh
tests/bench_images.sh 4        # thread count as first argument
```

It is not registered with CTest and is skipped when `Images/` is absent.

## Installation

No install target is configured. Copy `build/jpegopt` where you want it; it is
statically linked against the vendored dependencies, so the binary is
self-contained. `build/jpegopt --version` confirms a working build.

## Troubleshooting

**First build takes several minutes.** Expected: libjpeg-turbo compiles as part
of the parent build.

**`nasm` warnings or missing SIMD.** Harmless. Without nasm, libjpeg-turbo uses
its portable C path.

**Tests fail immediately with a linker error about `transupp.h`.** The
`jpeglib.h`/`transupp.h` includes come from the vendored turbo build; a stale
`build/` directory from an older layout can break this. Remove `build/` and
reconfigure.

**Temp directory errors.** `write_atomic` creates a temporary file next to each
output, so the output directory must be writable even in runs that write. For a
bare filename with no directory component (`jpegopt photo.jpg`) the temp file is
staged in the current directory. Use `--dry-run` when you only want a report,
and `--temp-dir` to stage elsewhere.
