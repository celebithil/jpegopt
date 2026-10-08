# CLI reference

`jpegopt [options] <file|dir> ...`

`jpegopt --help` is authoritative; this document adds defaults, interactions and
semantics that the built-in help does not cover.

## Input handling

Inputs may be files or directories. Directories are walked recursively
(`skip_permission_denied`) and every regular file with extension `.jpg`,
`.jpeg`, `.jpe` or `.jfif` (case-insensitive) is collected
(`collect_inputs` in `src/cli.cpp`). A directly named file is taken regardless of its
extension, so a JPEG named `.png` is processed (and fails at decode if it is not
a JPEG).

Duplicate paths are not deduplicated: naming the same file twice processes it
twice and reports it twice.

## Options

### Output control

| Flag | Default | Effect |
|---|---|---|
| (none) | — | Write `<name>.opt.<ext>` next to each source. The original extension is **preserved**: `photo.JPEG` → `photo.opt.JPEG`, `x.jpe` → `x.opt.jpe`, `y.jfif` → `y.opt.jfif` (`util::output_path_for` in `src/util.cpp`). |
| `-i, --in-place` | off | Overwrite the source file with the best result. Its mode and timestamps are preserved automatically. |
| `-p, --preserve` | off | Additionally copy the source's mode and timestamps onto *freshly created* outputs. Has no effect in in-place mode (that already preserves them). |
| `--dry-run` | off | Report only; nothing is written. In the report, `changed` still shows whether a smaller verified candidate was found, and `best_size` the gain it would produce. |

### Search control

| Flag | Default | Effect |
|---|---|---|
| `--arith` | off | Add 20 progressive + 1 sequential arithmetic-coding candidates (45 total instead of 24). Arithmetic output is used only when strictly smaller. See [limitations.md](limitations.md). |
| `-T, --threshold N` | `0` | Keep the original unless savings reach `N%`. Accepts decimals (`2.5`). Values outside `[0, 100]` are rejected. `0` means "write whenever the candidate is smaller". |
| `-t, --threads N` | `cores/4`, clamped to 1–4 | Worker threads. `N` must be a non-negative integer; anything else (including a negative value) is rejected with exit 2. `0` selects the automatic pool. Values above the cap (64) are clamped, not rejected, so `-t $(nproc)` stays harmless on large hosts. |
| `--temp-dir PATH` | system temp | Directory for jpegopt's temporary files. The winning result is staged there before being published, so on a different filesystem from the output it is copied across and then renamed into place. Defaults to a per-process directory under the system temp dir. |

### Metadata

| Flag | Default | Effect |
|---|---|---|
| `--strip-metadata` | off | Remove **all** APP/COM markers via the fast path (markers are never copied). |
| `--strip-exif` | off | Remove EXIF (APP1) markers. |
| `--strip-xmp` | off | Remove XMP (APP1) markers. |
| `--strip-icc` | off | Remove ICC colour profile (APP2) markers. |
| `--strip-iptc` | off | Remove IPTC/Photoshop (APP13) markers. |
| `--strip-adobe` | off | Remove Adobe (APP14) markers. |
| `--strip-jfif` | off | Remove JFIF (APP0) markers. |
| `--strip-jfxx` | off | Remove JFXX (APP0 extension) markers. |
| `--strip-com` | off | Remove comment (COM) markers. |

Granular flags combine, and EXIF/XMP are distinguished by payload signature even
though both are APP1. `--strip-metadata` supersedes the granular flags (it
removes everything, so the granular flags add nothing). See
[metadata-stripping.md](metadata-stripping.md).

Stripping never affects pixels and never affects verification.

### Input lists

| Flag | Default | Effect |
|---|---|---|
| `--files-from F` | — | Read additional input paths from file `F`, one per line. May be repeated. Blank lines and lines starting with `#` are ignored (`load_list_file` / `load_list_stdin` in `src/cli.cpp`). |
| `--files-stdin` | off | Read additional input paths from standard input, same rules. |

Paths from a list are resolved exactly like command-line paths (relative to the
current working directory) and merged with them; the processing order is
command-line order followed by list order.

### Reporting

| Flag | Default | Effect |
|---|---|---|
| `--json` | off | Emit a single JSON array; suppress human-readable output. See [json-output.md](json-output.md). |
| `--show-all` | off | In text mode, list every verified candidate size per file. Has no effect in `--json` mode, where `candidates` is always present. |
| `-v, --verbose` | off | Write per-file diagnostics to **stderr**: candidates verified, the winning method, sizes, savings, elapsed time, and why a file was left unchanged. stdout is untouched, so `--json` output stays machine-readable. |
| `-V, --version` | — | Print the version and exit 0. |
| `-h, --help` | — | Print usage and exit 0. |

`--version`/`--help` win over the rest of the line, but option parsing is a
single pass, so an unknown option is still reported (exit 2) before `-V`/`-h`
takes effect — `jpegopt --version --nope` fails.

## Exit codes

| Code | Meaning |
|---|---|
| `0` | All files processed without error (files may have been unchanged). |
| `1` | At least one file failed. Per-file reasons are in the report's `error` field. |
| `2` | Command-line error: unknown option, missing option value, invalid `--threshold`, no inputs, or no JPEG files found. The offending message is printed to stderr together with usage. |

## Interactions worth knowing

- `method` and `arith` in the JSON report are empty/`false` whenever no smaller
  verified candidate was **selected** — including when the threshold blocked it.
  A non-empty `method` with `changed: false` means "a smaller candidate existed,
  but `-T` kept the original".
- `--strip-metadata` fills `stripped_markers` with the categories it removes
  (the source is classified against an all-categories policy), so it is no
  longer empty on a file that carries strippable markers.
- `-t` bounds are validated (see the Search control table); `-T` rejects values
  outside `[0, 100]`.
- In-place (`-i`) publishing goes through the same atomic temp-file + `rename`
  path as every other write, so the source inode is replaced: a read-only file
  is still swapped out, and a hardlinked source loses its extra link. See
  [limitations.md](limitations.md).
- `--strip-metadata` + any `--strip-*`: the granular flags are redundant.
- `--dry-run` + `--json`: the standard way to measure. `changed` is `true` for
  files that would improve — nothing is written, so re-read the inputs after a
  dry run only if you dropped the flag.
- `--in-place` + `--dry-run`: no write happens; `-i` only selects the output
  path, and that selection is moot when nothing is written.
- `--arith` + `--strip-*`: stripping is applied to arithmetic candidates too, and
  they are verified like the rest.
- `-T` is applied to the winning candidate only, after selection.

## Examples

```sh
# Survey a tree without touching it, machine-readable
jpegopt --dry-run --json /photos > report.json

# Real in-place run, arithmetic, require a meaningful gain, drop EXIF
jpegopt -i --arith -T 2 --strip-exif /photos

# Only report files above a size, from a list, single-threaded for determinism
find /photos -size +2M -name '*.jpg' > list.txt
jpegopt --files-from list.txt -t 1 --dry-run
```
