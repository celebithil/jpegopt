# Metadata stripping

jpegopt can remove JPEG metadata markers by category. Pixels are never affected:
the candidate is still decoded and verified pixel-identical after stripping, and
stripping runs *before* verification so a bad strip can never produce a
"verified" wrong result.

## Category table

Categories are distinguished by marker code **and** payload signature
(`src/jpeg_reader.cpp`, `classify_segment`). The signature matters because
several categories share a marker code:

| Flag | Marker | Payload signature | Notes |
|---|---|---|---|
| `--strip-jfif` | APP0 (`0xE0`) | `JFIF\0` | Standard JFIF header |
| `--strip-jfxx` | APP0 (`0xE0`) | `JFXX\0` | JFIF extension |
| `--strip-exif` | APP1 (`0xE1`) | `Exif\0\0` | |
| `--strip-xmp` | APP1 (`0xE1`) | `http://ns.adobe.com/` | XMP and XMP extension packets |
| `--strip-icc` | APP2 (`0xE2`) | `ICC_PROFILE\0` | |
| `--strip-iptc` | APP13 (`0xED`) | `Photoshop 3.0\0` | |
| `--strip-adobe` | APP14 (`0xEE`) | `Adobe\0` | |
| `--strip-com` | COM (`0xFE`) | — | No signature needed |

**Consequence:** `--strip-exif` does *not* remove "all APP1", and it does not
remove XMP — even though XMP is also APP1. Conversely, an APP1 that carries
neither signature (some vendor-specific blocks) is treated as unknown and kept.

Unrecognised APPn markers are **kept** by default; there is no "strip everything
except…" option other than `--strip-metadata`, which removes all of them.

## Two mechanisms

**`--strip-metadata` (fast path).** The encoder is told not to copy markers at
all (`JCOPYOPT_NONE` in the `jcopy_markers_*` calls in `src/transcoder.cpp`), so they never enter the
output. This is the cheapest way to drop all metadata.

**Granular `--strip-*` (post-encode).** The candidate is encoded with markers
intact, then the header region is rewritten to remove only the flagged
categories (`strip_markers` in `src/jpeg_reader.cpp`). This is what makes
per-category stripping — and EXIF-vs-XMP disambiguation — possible.

Both paths run before verification and neither can affect decoded pixels.

## What the report tells you

The report's `stripped_markers` field lists the categories the policy matched
**in the source file**, computed by `classify_strippable` before any encoding
(the `classify_strippable` call in `process_file`). It is a forecast of what the policy targets, not a
receipt of what a particular output lost:

- It is independent of which candidate won.
- It is populated even under `--dry-run` and even if the file is left unchanged.
- A category absent from the list means the source did not contain a marker of
  that category (so stripping it was a no-op).

In the text report the same information appears as `(stripped: exif,com)` for a
granular run, or `(metadata stripped)` for `--strip-metadata`.

## Which markers actually cost size

Metadata is usually small, but on camera files it can be tens of kilobytes
(EXIF with embedded thumbnails, ICC profiles, XMP). Stripping can therefore win
a percent or two on its own, on top of the entropy-coding savings. On files with
no markers, `--strip-*` costs nothing and changes nothing.

## Guidelines

- Keep EXIF if you care about camera orientation and GPS.
- Keep JFIF if downstream software relies on it for colour/geometry hints.
- Keep ICC when colour management matters; removing it can shift appearance in
  colour-managed viewers.
- XMP is the usual safe thing to drop for web delivery; Adobe APP14 and IPTC
  matter mainly in print workflows.
- To know what a file actually carries before stripping, run once with
  `--dry-run --json` and read `stripped_markers`.
