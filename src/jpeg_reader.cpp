// SPDX-License-Identifier: Apache-2.0

#include "jpeg_reader.h"

#include <cstring>

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

// 0xC0..0xCF is not a contiguous SOF range: 0xC4 (DHT), 0xC8 (JPG) and
// 0xCC (DAC) are not frame headers, so the numeric span must be filtered.
bool is_sof_marker(uint8_t code) {
    return code >= kSof0 && code <= kSof11 && code != 0xC4 && code != 0xC8 && code != 0xCC;
}

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

            if (!have_frame && is_sof_marker(code) && pos + 9 <= data.size()) {
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
            // The SOS segment needs bytes up to seg+4 (Ns) and, once the
            // payload length is known, the 3 spectral-selector bytes.
            if (seg + 5 > data.size()) return false;
            const size_t len = (data[seg + 2] << 8) | data[seg + 3];
            const int ns = data[seg + 4];
            // SOS payload: Ns, Ns*(cs, td/ta), Ss, Se, Ah/Al.
            if (len < 2 + 1 + 2 * ns + 3 || seg + 2 + len > data.size()) return false;
            // The component-id bytes live inside the segment the length above
            // already proved present, so this loop cannot read past data.size().
            if (seg + 5 + 2 * static_cast<size_t>(ns) + 3 > seg + 2 + len) return false;
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

namespace {

enum class MarkerCat { kOther, kJfif, kJfxx, kExif, kXmp, kIcc, kIptc, kAdobe, kCom };

// `seg` points at a length-prefixed segment (0xFF, code, L, payload...).
MarkerCat classify_segment(const std::vector<uint8_t>& data, size_t seg) {
    const uint8_t code = data[seg + 1];
    const size_t len = (data[seg + 2] << 8) | data[seg + 3];
    const size_t payload = seg + 4;
    if (len < 2) return MarkerCat::kOther;

    auto has = [&](const char* s) -> bool {
        size_t n = 0;
        while (s[n] != '\0') ++n;
        return payload + n <= data.size() &&
               memcmp(data.data() + payload, s, n) == 0;
    };

    switch (code) {
        case 0xE0:  // APP0
            if (has("JFIF\0")) return MarkerCat::kJfif;
            if (has("JFXX\0")) return MarkerCat::kJfxx;
            return MarkerCat::kOther;
        case 0xE1:  // APP1
            if (has("Exif\0\0")) return MarkerCat::kExif;
            if (has("http://ns.adobe.com/")) return MarkerCat::kXmp;
            return MarkerCat::kOther;
        case 0xE2:  // APP2
            if (has("ICC_PROFILE\0")) return MarkerCat::kIcc;
            return MarkerCat::kOther;
        case 0xED:  // APP13
            if (has("Photoshop 3.0\0")) return MarkerCat::kIptc;
            return MarkerCat::kOther;
        case 0xEE:  // APP14
            if (has("Adobe\0")) return MarkerCat::kAdobe;
            return MarkerCat::kOther;
        case 0xFE:  // COM
            return MarkerCat::kCom;
        default:
            return MarkerCat::kOther;
    }
}

bool category_flagged(MarkerCat cat, const MarkerPolicy& policy) {
    switch (cat) {
        case MarkerCat::kJfif: return policy.strip_jfif;
        case MarkerCat::kJfxx: return policy.strip_jfxx;
        case MarkerCat::kExif: return policy.strip_exif;
        case MarkerCat::kXmp: return policy.strip_xmp;
        case MarkerCat::kIcc: return policy.strip_icc;
        case MarkerCat::kIptc: return policy.strip_iptc;
        case MarkerCat::kAdobe: return policy.strip_adobe;
        case MarkerCat::kCom: return policy.strip_com;
        default: return false;
    }
}

// Walks length-prefixed segments in the header region (before the first SOS).
// `handle` receives every segment start; returns false to stop the walk.
template <typename F>
bool walk_header(const std::vector<uint8_t>& data, F&& handle) {
    if (data.size() < 4 || data[0] != 0xFF || data[1] != kSoi) {
        return false;
    }
    size_t pos = 2;
    while (pos + 1 < data.size()) {
        while (pos < data.size() && data[pos] != 0xFF) {
            ++pos;
        }
        if (pos + 1 >= data.size()) {
            return false;
        }
        uint8_t c = data[pos + 1];
        if (c == 0xFF) {  // fill byte
            ++pos;
            continue;
        }
        if (c == kEoi || c == kSos) {
            return true;
        }
        if (c == 0x01 || (c >= 0xD0 && c <= 0xD7) || c == kSoi) {
            pos += 2;
            continue;
        }
        if (pos + 4 > data.size()) {
            return false;
        }
        const size_t len = (data[pos + 2] << 8) | data[pos + 3];
        if (len < 2 || pos + 2 + len > data.size()) {
            return false;
        }
        if (!handle(pos)) {
            return true;
        }
        pos += 2 + len;
    }
    return false;
}

}  // namespace

std::string MarkerStripped::summary() const {
    std::string s;
    auto add = [&](bool v, const char* name) {
        if (v) {
            if (!s.empty()) s += ",";
            s += name;
        }
    };
    add(jfif, "jfif");
    add(jfxx, "jfxx");
    add(exif, "exif");
    add(xmp, "xmp");
    add(icc, "icc");
    add(iptc, "iptc");
    add(adobe, "adobe");
    add(com, "com");
    return s;
}

MarkerStripped classify_strippable(const std::vector<uint8_t>& data,
                                   const MarkerPolicy& policy) {
    MarkerStripped out;
    auto flag = [&](MarkerCat cat) {
        switch (cat) {
            case MarkerCat::kJfif: out.jfif = true; break;
            case MarkerCat::kJfxx: out.jfxx = true; break;
            case MarkerCat::kExif: out.exif = true; break;
            case MarkerCat::kXmp: out.xmp = true; break;
            case MarkerCat::kIcc: out.icc = true; break;
            case MarkerCat::kIptc: out.iptc = true; break;
            case MarkerCat::kAdobe: out.adobe = true; break;
            case MarkerCat::kCom: out.com = true; break;
            default: break;
        }
    };
    walk_header(data, [&](size_t seg) {
        MarkerCat cat = classify_segment(data, seg);
        if (category_flagged(cat, policy)) {
            flag(cat);
        }
        return true;
    });
    return out;
}

void strip_markers(std::vector<uint8_t>& data, const MarkerPolicy& policy) {
    if (data.size() < 4 || data[0] != 0xFF || data[1] != kSoi) {
        return;
    }
    std::vector<uint8_t> out;
    out.reserve(data.size());
    out.push_back(0xFF);
    out.push_back(kSoi);

    size_t pos = 2;
    while (pos + 1 < data.size()) {
        size_t mark = pos;
        while (mark < data.size() && data[mark] != 0xFF) {
            ++mark;
        }
        out.insert(out.end(), data.begin() + pos, data.begin() + mark);
        if (mark + 1 >= data.size()) {
            break;
        }
        const uint8_t c = data[mark + 1];
        if (c == 0xFF) {  // fill byte; keep, continue scanning
            out.push_back(0xFF);
            pos = mark + 1;
            continue;
        }
        if (c == kEoi || c == kSos) {
            out.insert(out.end(), data.begin() + mark, data.end());
            break;
        }
        if (c == 0x01 || (c >= 0xD0 && c <= 0xD7)) {
            out.push_back(0xFF);
            out.push_back(c);
            pos = mark + 2;
            continue;
        }
        if (mark + 4 > data.size()) {
            out.insert(out.end(), data.begin() + mark, data.end());
            break;
        }
        const size_t len = (data[mark + 2] << 8) | data[mark + 3];
        if (len < 2 || mark + 2 + len > data.size()) {
            out.insert(out.end(), data.begin() + mark, data.end());
            break;
        }
        MarkerCat cat = classify_segment(data, mark);
        if (!category_flagged(cat, policy)) {
            out.insert(out.end(), data.begin() + mark, data.begin() + mark + 2 + len);
        }
        pos = mark + 2 + len;
    }
    data = std::move(out);
}
