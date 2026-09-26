---
name: Correctness bug (pixel mismatch)
about: The output is not pixel-identical to the source
title: "[correctness] "
labels: bug, critical
assignees: ''
---

<!--
This is the most serious class of bug in this project: jpegopt promises that
every written result decodes to exactly the same pixels as the source. Use this
template whenever that promise appears to be broken, and please attach the
triggering file — a reproducer is required to fix it.
-->

## What happened

<!-- Describe the mismatch: how did you detect it? Visual inspection, a
     comparison tool, a checksum of decoded pixels, ... -->

## Command line

```sh
jpegopt ...
```

## Reproducer file

<!-- REQUIRED. Attach the smallest JPEG that shows the problem, or provide a
     reliable download link. State its size, and whether it is a real photo or
     synthetic. -->

- File name / link:
- Dimensions and colour space (e.g. 3072x4096, YCbCr 4:2:0):
- Entropy coding of the source (baseline / progressive / arithmetic):
- Approximate size:

## Verification you performed

<!-- How did you compare? Which tool? Include the output if available. -->

## Environment

- jpegopt version (`jpegopt -V`):
- OS / distribution:
- Compiler (if built from source):

## Additional context

<!-- Which flags were used? Did the same input behave correctly with fewer
     flags (e.g. without --arith, or without --strip-*)? Does it still
     reproduce with --dry-run? -->
