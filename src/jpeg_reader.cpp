// SPDX-License-Identifier: Apache-2.0

#include "jpeg_reader.h"

namespace {

// Marker byte values used in the classification logic.
constexpr uint8_t kSoi = 0xD8;
constexpr uint8_t kEoi = 0xD9;
constexpr uint8_t kSos = 0xDA;
constexpr uint8_t kDri = 0xDD;
constexpr uint8_t kSof0 = 0xC0;  // baseline
constexpr uint8_t kSof1 = 0xC1;  // extended sequential
constexpr uint8_t kSof2 = 0xC2;  // progressive
constexpr uint8_t kSof3 = 0xC3;  // lossless
constexpr uint8_t kSof9 = 0xC9;  // extended sequential, arithmetic
constexpr uint8_t kSof10 = 0xCA; // progressive, arithmetic
constexpr uint8_t kSof11 = 0xCB; // lossless, arithmetic

}  // namespace

JpegInfo read_jpeg_info(const std::vector<uint8_t>& data) {
    JpegInfo info;
    if (data.size() < 4 || data[0] != 0xFF || data[1] != kSoi) {
        info.error = "not a JPEG file (bad SOI)";
        return info;
    }

    size_t pos = 2;
    bool have_frame = false;
    while (pos + 1 < data.size()) {
        // Find a marker (skip fill bytes 0xFF 0xFF ...).
        while (pos < data.size() && data[pos] != 0xFF) {
            ++pos;
        }
        if (pos + 1 >= data.size()) {
            break;
        }
        uint8_t code = data[pos + 1];
        if (code == 0xFF) {  // fill byte
            ++pos;
            continue;
        }
        if (code == kSoi) {  // unexpected nested SOI
            pos += 2;
            continue;
        }
        if (code == kEoi) {
            break;
        }

        const bool standalone = code == 0x01 || (code >= 0xD0 && code <= 0xD7) || code == 0xD8 ||
                                code == 0xD9;
        if (code == kSos) {
            break;  // entropy-coded data follows; header scanning stops here
        }

        if (code == kDri && pos + 6 <= data.size()) {
            info.restart_interval = (data[pos + 4] << 8) | data[pos + 5];
        }

        if (code >= kSof0 && code <= kSof11) {
            if (code == kSof0) info.is_baseline = true;
            if (code == kSof2) info.is_progressive = true;
            if (code == kSof9 || code == kSof10 || code == kSof11) info.is_arithmetic = true;

            if (!have_frame && pos + 9 <= data.size()) {
                have_frame = true;
                info.precision = data[pos + 4];
                info.height = (data[pos + 5] << 8) | data[pos + 6];
                info.width = (data[pos + 7] << 8) | data[pos + 8];
                if (pos + 9 < data.size()) {
                    info.num_components = data[pos + 9];
                }
            }
        }

        if (standalone) {
            pos += 2;
            continue;
        }

        if (pos + 4 > data.size()) {
            break;
        }
        size_t len = (data[pos + 2] << 8) | data[pos + 3];
        if (len < 2) {
            break;
        }
        pos += 2 + len;
    }

    if (!have_frame) {
        info.error = "JPEG header has no frame (SOF) marker";
        return info;
    }
    if (info.width <= 0 || info.height <= 0) {
        info.error = "JPEG has invalid dimensions";
        return info;
    }

    info.valid = true;
    return info;
}

int count_markers(const std::vector<uint8_t>& data, uint8_t code) {
    if (data.size() < 4 || data[0] != 0xFF || data[1] != kSoi) {
        return 0;
    }
    int count = 0;
    size_t pos = 2;
    while (pos + 1 < data.size()) {
        while (pos < data.size() && data[pos] != 0xFF) {
            ++pos;
        }
        if (pos + 1 >= data.size()) {
            break;
        }
        uint8_t c = data[pos + 1];
        if (c == 0xFF) {
            ++pos;
            continue;
        }
        if (c == kEoi || c == kSos) {
            break;
        }
        const bool standalone = c == 0x01 || (c >= 0xD0 && c <= 0xD7) || c == 0xD8 ||
                                c == 0xD9;
        if (standalone) {
            pos += 2;
            continue;
        }
        if (pos + 4 > data.size()) {
            break;
        }
        size_t len = (data[pos + 2] << 8) | data[pos + 3];
        if (len < 2) {
            break;
        }
        if (c == code) {
            ++count;
        }
        pos += 2 + len;
    }
    return count;
}

bool extract_scan_script(const std::vector<uint8_t>& data, std::vector<JpegScan>& scans) {
    scans.clear();
    if (data.size() < 4 || data[0] != 0xFF || data[1] != kSoi) {
        return false;
    }

    // SOF component id -> 0-based component index.
    int id_to_index[256];
    for (int i = 0; i < 256; ++i) id_to_index[i] = -1;

    size_t pos = 2;
    bool entropy = false;  // inside the entropy-coded data of the current scan
    while (pos + 1 < data.size()) {
        while (pos < data.size() && data[pos] != 0xFF) {
            ++pos;
        }
        if (pos + 1 >= data.size()) {
            return false;
        }
        uint8_t c = data[pos + 1];

        if (entropy) {
            // 0xFF 0x00 is a byte-stuffed value inside entropy data; RST
            // markers are standalone; anything else ends the current scan.
            if (c == 0x00) {
                pos += 2;
                continue;
            }
            if (c >= 0xD0 && c <= 0xD7) {
                pos += 2;
                continue;
            }
            if (c == 0xFF) {
                ++pos;
                continue;
            }
            entropy = false;
        }
        if (c == 0xFF) {
            ++pos;
            continue;
        }
        if (c == kSoi) {
            pos += 2;
            continue;
        }
        if (c == kEoi) {
            return true;
        }
        if (c == 0x01 || (c >= 0xD0 && c <= 0xD7)) {
            pos += 2;
            continue;
        }

        if (c == kSos) {
            const size_t seg = pos;
            if (seg + 4 > data.size()) return false;
            const size_t len = (data[seg + 2] << 8) | data[seg + 3];
            const int ns = data[seg + 4];
            // SOS payload: Ns, Ns*(cs, td/ta), Ss, Se, Ah/Al.
            if (len < 2 + 1 + 2 * ns + 3 || seg + 2 + len > data.size()) return false;
            JpegScan s;
            s.comps.reserve(ns);
            for (int i = 0; i < ns; ++i) {
                const uint8_t id = data[seg + 5 + 2 * i];
                if (id_to_index[id] < 0) return false;
                s.comps.push_back(static_cast<uint8_t>(id_to_index[id]));
            }
            const size_t spectral = seg + 5 + 2 * ns;
            s.ss = data[spectral];
            s.se = data[spectral + 1];
            s.ah = data[spectral + 2] >> 4;
            s.al = data[spectral + 2] & 0x0F;
            scans.push_back(std::move(s));
            pos = seg + 2 + len;
            entropy = true;
            continue;
        }

        // SOF markers (0xC4 = DHT, 0xC8 = JPG, 0xCC = DAC are not frames).
        const bool is_sof = (c >= 0xC0 && c <= 0xCF) && c != 0xC4 && c != 0xC8 && c != 0xCC;
        if (is_sof && pos + 10 <= data.size()) {
            const int ncomp = data[pos + 9];
            const size_t comps = pos + 10;
            if (comps + static_cast<size_t>(3 * ncomp) > data.size()) return false;
            for (int i = 0; i < ncomp; ++i) {
                id_to_index[data[comps + 3 * i]] = i;
            }
        }

        if (pos + 4 > data.size()) {
            return false;
        }
        const size_t len = (data[pos + 2] << 8) | data[pos + 3];
        if (len < 2) {
            return false;
        }
        pos += 2 + len;
    }
    return false;
}
