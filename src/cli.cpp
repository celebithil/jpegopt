// SPDX-License-Identifier: Apache-2.0

#include "cli.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool is_jpeg_extension(const std::string& name) {
    std::string lower = name;
    for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return lower == ".jpg" || lower == ".jpeg" || lower == ".jpe" || lower == ".jfif";
}

std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Reads one path per line; blank lines and lines starting with '#' are skipped.
bool load_list_file(const std::string& path, std::vector<std::string>& out,
                    std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "cannot open file list " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        out.push_back(line);
    }
    return true;
}

bool load_list_stdin(std::vector<std::string>& out, std::string& err) {
    std::string line;
    while (std::getline(std::cin, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        out.push_back(line);
    }
    if (std::cin.bad()) {
        err = "error reading file list from stdin";
        return false;
    }
    return true;
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
        "      --strip-exif    strip EXIF (APP1) markers only\n"
        "      --strip-xmp     strip XMP (APP1) markers only\n"
        "      --strip-icc     strip ICC color profile (APP2) markers\n"
        "      --strip-iptc    strip IPTC/Photoshop (APP13) markers\n"
        "      --strip-adobe   strip Adobe (APP14) markers\n"
        "      --strip-jfif    strip JFIF (APP0) markers\n"
        "      --strip-jfxx    strip JFXX (APP0 extension) markers\n"
        "      --strip-com     strip comment (COM) markers\n"
        "  -T, --threshold N   keep the original unless savings reach N%\n"
        "      --files-from F  read the list of files to process from file F\n"
        "      --files-stdin   read the list of files to process from stdin\n"
        "  -p, --preserve      copy source timestamps/mode to new outputs\n"
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
            const char* v = argv[++i];
            char* end = nullptr;
            long n = strtol(v, &end, 10);
            if (end == v || *end != '\0' || n < 0) {
                err = "invalid thread count (non-negative integer): " + std::string(v);
                return false;
            }
            if (n > kMaxThreads) n = kMaxThreads;
            opts.threads = static_cast<int>(n);
        } else if (a == "-i" || a == "--in-place") {
            opts.in_place = true;
        } else if (a == "--dry-run") {
            opts.dry_run = true;
        } else if (a == "--arith") {
            opts.allow_arith = true;
        } else if (a == "--strip-metadata") {
            opts.strip_metadata = true;
        } else if (a == "--strip-exif") {
            opts.markers.strip_exif = true;
        } else if (a == "--strip-xmp") {
            opts.markers.strip_xmp = true;
        } else if (a == "--strip-icc") {
            opts.markers.strip_icc = true;
        } else if (a == "--strip-iptc") {
            opts.markers.strip_iptc = true;
        } else if (a == "--strip-adobe") {
            opts.markers.strip_adobe = true;
        } else if (a == "--strip-jfif") {
            opts.markers.strip_jfif = true;
        } else if (a == "--strip-jfxx") {
            opts.markers.strip_jfxx = true;
        } else if (a == "--strip-com") {
            opts.markers.strip_com = true;
        } else if (needs_value("-T", "--threshold")) {
            const char* v = argv[++i];
            char* end = nullptr;
            double t = strtod(v, &end);
            if (end == v || *end != '\0' || t < 0.0 || t > 100.0) {
                err = "invalid threshold (0-100): " + std::string(v);
                return false;
            }
            opts.threshold_pct = t;
        } else if (needs_value("--files-from", "--files-from")) {
            opts.list_files.push_back(argv[++i]);
        } else if (a == "--files-stdin") {
            opts.read_stdin_list = true;
        } else if (a == "-p" || a == "--preserve") {
            opts.preserve = true;
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

    for (const auto& f : opts.list_files) {
        if (!load_list_file(f, opts.inputs, err)) {
            return false;
        }
    }
    if (opts.read_stdin_list) {
        if (!load_list_stdin(opts.inputs, err)) {
            return false;
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
