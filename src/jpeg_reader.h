// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct JpegInfo {
    bool valid = false;
    bool is_progressive = false;
    bool is_arithmetic = false;
    bool is_baseline = false;
    int width = 0;
    int height = 0;
    int num_components = 0;
    int precision = 0;
    int restart_interval = 0;  // 0 when no DRI marker present
    std::string error;
};

// One progressive scan as parsed from an SOS marker.
struct JpegScan {
    std::vector<uint8_t> comps;  // 0-based component indices, in scan order
    int ss = 0;
    int se = 0;
    int ah = 0;
    int al = 0;
};

// Parses the JPEG header enough to classify the file.
JpegInfo read_jpeg_info(const std::vector<uint8_t>& data);

// Walks the whole file and collects every SOS marker in order (the source's
// scan script). Fills `scans` and returns true on success (SOI..EOI walked).
// False is returned for malformed input or when no EOI is found.
bool extract_scan_script(const std::vector<uint8_t>& data, std::vector<JpegScan>& scans);

// Counts occurrences of a marker `code` (e.g. 0xE1 for APP1) in the header
// region before the first SOS marker. Standalone markers and SOS itself are
// not counted for `code`.
int count_markers(const std::vector<uint8_t>& data, uint8_t code);
