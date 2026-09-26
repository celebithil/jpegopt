# Security Policy

## Supported versions

Only the latest released version receives fixes. Please reproduce against
`main` before reporting: the bug may already be fixed.

| Version | Supported |
|---|---|
| 1.0.x | yes |
| < 1.0 | no |

## What counts as a security issue

jpegopt processes untrusted binary input — JPEG files from the internet, camera
cards, message attachments. The interesting surface is therefore parsing and
decoding, not the output:

- **Memory safety.** Out-of-bounds read/write, buffer overflow, use-after-free,
  or a crash (segfault) triggered by a crafted or truncated JPEG. This is the
  primary concern: the tool links vendored C code (libjpeg-turbo, guetzli) and
  contains its own marker parser (`src/jpeg_reader.cpp`).
- **Uncontrolled resource use.** A small input causing unbounded memory or CPU
  consumption (denial of service), e.g. a decompression-bomb-shaped image.
- **Path handling.** Writing an output outside the expected directory, or
  following a symlink in a way that overwrites an unintended file.
- **Losslessness violations with a security angle.** Producing an output that
  is *not* pixel-identical while reporting success. (A plain correctness bug is
  an ordinary bug, not a security issue — use the bug template.)

A file that jpegopt rejects with an error is **not** a vulnerability; failing
safely on hostile input is the intended behaviour.

## What is out of scope

- Lossy-quality or file-size opinions.
- Crashes caused by a genuinely corrupt file that libjpeg-turbo rejects with a
  normal error.
- Findings that require an already-compromised build environment.
- Denial of service from a legitimately huge (multi-gigapixel) image.

## Reporting

Report privately via GitHub's **Security → Report a vulnerability** tab on the
repository (`Security` → `Advisories` → `Report a vulnerability`). Do not open
a public issue for a suspected vulnerability.

Please include:

- the `jpegopt -V` output,
- the exact command line,
- the triggering file (a minimal reproducer is strongly preferred),
- the observed behaviour (crash address/sanitizer output if available),
- your platform and compiler.

You can expect an acknowledgement within a few days. Fixes for confirmed issues
land in a patch release and are credited in `CHANGELOG.md` unless you prefer
otherwise.

## Hardening notes for users

- Run with a dedicated user and write access limited to the intended tree;
  jpegopt writes `<name>.opt.<ext>` next to each input.
- Prefer `--dry-run` first to see what would change.
- For hostile input, consider `--temp-dir` on a filesystem with space limits.
