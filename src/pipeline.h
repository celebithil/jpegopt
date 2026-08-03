// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PipelineOptions {
    // Arithmetic coding candidates are off by default; enable with --arith.
    bool allow_arith = false;
    bool dry_run = false;
    bool in_place = false;
    bool verbose = false;
    // Strip APP/COM markers (JFIF/EXIF/comments) from every candidate.
    // Pixels unchanged; metadata is lost.
    bool strip_metadata = false;
    // Empty = system temp directory
    std::string temp_dir;
};

struct FileResult {
    std::string path;
    bool ok = false;
    bool changed = false;
    uint64_t original_size = 0;
    uint64_t best_size = 0;
    // Winner method tag: "progressive", "progressive-min",
    // "progressive-bands", "progressive-fine", "guetzli", "arith",
    // "progressive+arith", and the corresponding "-min"/"-bands"/"-fine"
    // arithmetic variants; empty when no candidate beat the original.
    std::string method;
    // Bytes of the winning candidate (when smaller than the original), so
    // callers can consume the optimized JPEG without reading it back from disk.
    std::vector<uint8_t> best_bytes;
    bool arith_used = false;
    bool arith_fell_back = false;
    // True when this run stripped APP/COM markers (see PipelineOptions).
    bool strip_metadata = false;
    std::string error;

    struct CandidateInfo {
        std::string method;
        uint64_t size = 0;
        bool arith = false;
    };
    // Every valid (pixel-verified) candidate produced, with its size.
    std::vector<CandidateInfo> candidates;
};

FileResult process_file(const std::string& path, const PipelineOptions& opts);
