# guetzli: local patch for jpegopt

`vendor/guetzli` is google/guetzli (Apache-2.0) vendored in-tree as a
regular directory (not a submodule) because it carries one local modification:

## jpeg_data_reader.cc

`#define fprintf(...) do {} while (0)` (around line 28): guetzli's decoder
prints decoding diagnostics to stderr. jpegopt runs many candidate encoders
concurrently on worker threads; silencing the prints keeps stdout/stderr
deterministic and per-file output unambiguous under multi-threading.

Nothing else differs from upstream. Everything else here is verbatim google/
guetzli as of `214f2bb` (the `jpeg_data_reader.cc`, `jpeg_data_writer.cc`,
`entropy_encode.cc`, `jpeg_huffman_decode.cc`, `jpeg_data.cc` files jpegopt
compiles; the other files are retained for context but not built).

To regenerate the pristine upstream tree: clone https://github.com/google/guetzli
at commit `214f2bb` and re-apply the single-line `fprintf` no-op.
