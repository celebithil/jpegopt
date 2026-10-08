// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "jpeg_reader.h"

#define JPEGOPT_VERSION "1.1.0"

// Upper bound for -t/--threads. The per-file workload allocates large buffers
// (a 12 MP frame costs ~250 MB across its candidate set), so an unbounded worker
// count is a memory hazard, not just a scheduling mistake. Requests above this
// are clamped rather than rejected, so `-t $(nproc)` stays harmless on big hosts.
static constexpr int kMaxThreads = 64;

struct CliOptions {
    bool help = false;
    bool version = false;
    int threads = 0;  // 0 = auto
    bool in_place = false;
    bool dry_run = false;
    bool allow_arith = false;
    bool json = false;
    bool verbose = false;
    bool show_all = false;
    bool strip_metadata = false;
    MarkerPolicy markers;
    // --threshold: minimum savings (percent) before a result is written.
    double threshold_pct = 0.0;
    bool preserve = false;
    bool read_stdin_list = false;  // --files-stdin
    std::vector<std::string> list_files;  // --files-from FILE
    std::string temp_dir;
    std::vector<std::string> inputs;
};

// Parses command line (excluding argv[0]). Returns false on unknown options.
bool parse_cli(int argc, char** argv, CliOptions& opts, std::string& err);

std::string usage_text();

// Recursively expands file/directory inputs into a list of JPEG files.
void collect_inputs(const std::vector<std::string>& inputs, std::vector<std::string>& files);
