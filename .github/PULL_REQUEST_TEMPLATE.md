# Pull request

<!-- Thanks for improving jpegopt. Correctness comes first: this is a lossless
     tool and every candidate must be pixel-identical before it is written. -->

## What this changes

<!-- Concise description. -->

## Checklist

- [ ] **Pixel-identity preserved.** Any new scan script, re-encode path or
      stripping mode decodes to exactly the same pixels as the source. The
      roundtrip suite passes, and for scan-script changes the change was checked
      against several real JPEGs.
- [ ] **Tests added or updated.** New candidates are added to the `required`
      list and/or the `sweep_styles` array in `tests/roundtrip_test.cpp`.
- [ ] **Candidate lists measured.** A new scan script is kept only if it wins at
      least one file in a corpus sweep (see `docs/scan-scripts.md`); the
      measurement is described in the PR.
- [ ] **Documentation updated.** New CLI flags appear in `usage_text()`
      (`src/cli.cpp`), `docs/cli.md` and `README.md`; new behaviour is reflected
      in `docs/limitations.md`; benchmark changes update `docs/benchmarks.md`.
- [ ] **CHANGELOG entry** added under `Unreleased`.
- [ ] **SPDX header** `SPDX-License-Identifier: Apache-2.0` on every new file.
- [ ] **Vendored code untouched.** `vendor/` is not modified. If a local patch
      is genuinely unavoidable, it is documented in `JPEGOPT_PATCH.md` and in
      `NOTICE` (as done for guetzli).
- [ ] Style matches the surrounding code: C++17, 4-space indent, no unrelated
      churn.

## Testing performed

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

<!-- Also describe any manual runs: corpora, flags, measured effect. -->

## Notes for reviewers

<!-- Anything non-obvious: trade-offs, rejected alternatives, follow-ups. -->
