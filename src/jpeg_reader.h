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

// Which marker categories to remove from the output. Each flag mirrors the
// --strip-* CLI option; metadata markers are preserved by default.
struct MarkerPolicy {
    bool strip_jfif = false;   // APP0 JFIF
    bool strip_jfxx = false;   // APP0 JFXX (JFIF extension)
    bool strip_exif = false;   // APP1 "Exif\0\0"
    bool strip_xmp = false;    // APP1 "http://ns.adobe.com/xap/..."
    bool strip_icc = false;    // APP2 "ICC_PROFILE\0"
    bool strip_iptc = false;   // APP13 "Photoshop 3.0\0"
    bool strip_adobe = false;  // APP14 "Adobe\0"
    bool strip_com = false;    // COM

    bool strips_anything() const {
        return strip_jfif || strip_jfxx || strip_exif || strip_xmp ||
               strip_icc || strip_iptc || strip_adobe || strip_com;
    }
};

// Which marker categories were found in a file and would be stripped by a
// policy. Used for reporting.
struct MarkerStripped {
    bool jfif = false;
    bool jfxx = false;
    bool exif = false;
    bool xmp = false;
    bool icc = false;
    bool iptc = false;
    bool adobe = false;
    bool com = false;

    bool any() const {
        return jfif || jfxx || exif || xmp || icc || iptc || adobe || com;
    }
    // Comma-joined category names actually stripped, e.g. "exif,com".
    std::string summary() const;
};

// Scans the header region of `data` and reports which marker categories
// selected by `policy` are present (and thus would be removed).
MarkerStripped classify_strippable(const std::vector<uint8_t>& data,
                                   const MarkerPolicy& policy);

// Removes the APP/COM segments flagged by `policy` from the header region of
// `data` (before the first SOS). Entropy-coded data and pixels are untouched.
void strip_markers(std::vector<uint8_t>& data, const MarkerPolicy& policy);
