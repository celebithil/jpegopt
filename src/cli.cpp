// SPDX-License-Identifier: Apache-2.0

#include "cli.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace {

bool is_jpeg_extension(const std::string& name) {
    std::string lower = name;
    for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return lower == ".jpg" || lower == ".jpeg" || lower == ".jpe" || lower == ".jfif";
}

}  // namespace

std::string usage_text() {
    return
        "Usage: jpegopt [options] <file|dir> ...\n"
        "\n"
        "Lossless JPEG optimization: progressive Huffman re-encode, optionally\n"
        "with arithmetic coding candidates, each verified pixel-identical.\n"
        "\n"
        "Options:\n"
        "  -t, --threads N     worker threads (default: auto)\n"
        "  -i, --in-place      overwrite the source file with the best result\n"
        "      --dry-run       report only, do not write anything\n"
        "      --arith         also try arithmetic coding candidates\n"
        "                      (Huffman is used by default)\n"
        "      --strip-metadata drop APP/COM markers (EXIF/JFIF/comments);\n"
        "                      pixels unchanged, metadata lost\n"
        "      --json          machine-readable JSON report\n"
        "      --show-all      list every candidate size per file\n"
        "      --temp-dir PATH directory for temporary files\n"
        "  -v, --verbose       verbose output\n"
        "  -V, --version       show version and exit\n"
        "  -h, --help          show this help\n"
        "\n"
        "Output (default): <name>.opt.jpg next to each source file.\n"
        "Each result is verified lossless (decoded pixels must be identical to\n"
        "the source before anything is written).\n"
        "Arithmetic-coded JPEGs are not supported by Chrome/Firefox/Photoshop;\n"
        "pass --arith only when you need the extra compression.\n";
}

bool parse_cli(int argc, char** argv, CliOptions& opts, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto needs_value = [&](const std::string& flag, const char* opt) -> bool {
            if (a == flag || a == opt) {
                if (i + 1 >= argc) {
                    err = "missing value for " + a;
                    return false;
                }
                return true;
            }
            return false;
        };

        if (a == "-h" || a == "--help") {
            opts.help = true;
        } else if (a == "-V" || a == "--version") {
            opts.version = true;
        } else if (needs_value("-t", "--threads")) {
            opts.threads = atoi(argv[++i]);
        } else if (a == "-i" || a == "--in-place") {
            opts.in_place = true;
        } else if (a == "--dry-run") {
            opts.dry_run = true;
        } else if (a == "--arith") {
            opts.allow_arith = true;
        } else if (a == "--strip-metadata") {
            opts.strip_metadata = true;
        } else if (a == "--json") {
            opts.json = true;
        } else if (a == "--show-all") {
            opts.show_all = true;
        } else if (a == "-v" || a == "--verbose") {
            opts.verbose = true;
        } else if (needs_value("--temp-dir", "--temp-dir")) {
            opts.temp_dir = argv[++i];
        } else if (!a.empty() && a[0] == '-') {
            err = "unknown option: " + a;
            return false;
        } else {
            opts.inputs.push_back(a);
        }
    }
    return true;
}

void collect_inputs(const std::vector<std::string>& inputs, std::vector<std::string>& files) {
    std::error_code ec;
    for (const auto& in : inputs) {
        std::filesystem::path p(in);
        if (!std::filesystem::exists(p, ec)) {
            continue;
        }
        if (std::filesystem::is_directory(p, ec)) {
            std::filesystem::recursive_directory_iterator it(
                p, std::filesystem::directory_options::skip_permission_denied, ec);
            std::filesystem::recursive_directory_iterator end;
            for (; it != end; it.increment(ec)) {
                if (it->is_regular_file(ec) &&
                    is_jpeg_extension(it->path().extension().string())) {
                    files.push_back(it->path().string());
                }
            }
        } else if (std::filesystem::is_regular_file(p, ec)) {
            files.push_back(in);
        }
    }
}
