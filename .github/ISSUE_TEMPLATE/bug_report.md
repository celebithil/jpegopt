---
name: Bug report
about: Something behaves unexpectedly
title: "[bug] "
labels: bug
assignees: ''
---

<!--
For a lossless tool, the most important question is whether pixels changed.
If a result is NOT pixel-identical, please use the "Correctness bug" template
instead — it asks for a reproducer file.
-->

## What happened

<!-- A clear description of the incorrect behaviour. -->

## What you expected

<!-- What the tool should have done instead. -->

## Reproduction

```sh
# exact command line, including all flags
jpegopt ...
```

## Environment

- jpegopt version (`jpegopt -V`):
- OS / distribution:
- Compiler (if built from source):
- Number of files in the run, and approximate total size:

## Input files

<!--
How were the inputs obtained (camera, download, generated)? If possible, give
the source or a link. Do NOT paste large binaries into the issue; attach a
single small sample if you can, or provide a download link.
-->

## Additional context

<!-- Warnings, partial output, anything else that helps. -->
