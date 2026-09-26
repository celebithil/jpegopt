# Contributing

Thanks for considering a contribution to jpegopt. This project optimizes
JPEGs losslessly, so **correctness is the top priority**: every candidate is
verified pixel-identical before it is considered a winner.

## Getting started

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/short-description`.
3. Make your changes.
4. Build and run the test suite:

   ```sh
   cmake -S . -B build
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ```

5. Submit a pull request with a clear description of the change.

## What to keep in mind

- **Pixel-identity is non-negotiable.** Any new scan script or re-encode path
  must produce output that decodes to exactly the same pixels as the source.
  Run the roundtrip tests and, for scan-script changes, verify against several
  real JPEGs.
- **Follow the existing style.** C++17, SPDX license headers
  (`SPDX-License-Identifier: Apache-2.0`) on every new file, 4-space indent,
  no unused includes, no code churn unrelated to the change.
- **Keep documentation in sync.** If you add a CLI flag, update the usage text
  in `src/cli.cpp`, `docs/cli.md` and the README block. If you change the
  pipeline, check `docs/architecture.md` and `docs/limitations.md`. If you add a
  benchmark result, update `docs/benchmarks.md` and the table in `README.md`.
- **Adding a scan script is a measurement decision.** A new `ScanStyle` is kept
  only if it wins at least one file in a corpus sweep; see
  [docs/scan-scripts.md](docs/scan-scripts.md) for the exact procedure and the
  list-pruning rule driven by `scripts/analyze_bench.py`.
- **Do not modify vendored code.** `vendor/` contains exact upstream copies
  (libjpeg-turbo, guetzli). Any needed local patch belongs in our code, not a
  vendored file; if a vendored patch is truly unavoidable, document it in
  `JPEGOPT_PATCH.md` and the `NOTICE` file as has been done for guetzli. See
  [vendor/README.md](vendor/README.md).

## Reporting issues

Please include the command line used, the output of `jpegopt -V`, and — if a
result is not pixel-identical — the source file (or a reduced reproducer). Use
the issue templates: a pixel mismatch is a **correctness bug**, and reproducing
it requires the triggering file, so please attach one.

For security issues, do not open a public issue — see
[SECURITY.md](SECURITY.md).