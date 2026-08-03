// SPDX-License-Identifier: Apache-2.0

#include "guetzli_encode.h"

#include "guetzli/jpeg_data.h"
#include "guetzli/jpeg_data_reader.h"
#include "guetzli/jpeg_data_writer.h"
#include "jpeg_reader.h"

namespace {

int append_to_vector(void* data, const uint8_t* buf, size_t len) {
    auto* v = static_cast<std::vector<uint8_t>*>(data);
    v->insert(v->end(), buf, buf + len);
    return static_cast<int>(len);
}

}  // namespace

bool guetzli_encode(const std::vector<uint8_t>& src, bool strip_metadata,
                    std::vector<uint8_t>& out, std::string& err) {
    using namespace guetzli;

    // guetzli's reader only understands Huffman-coded SOF markers; arithmetic
    // sources would otherwise be mis-parsed. Reject them up front so the
    // pipeline falls through to the transcode candidates.
    JpegInfo info = read_jpeg_info(src);
    if (!info.valid) {
        err = "guetzli: " + info.error;
        return false;
    }
    if (info.is_arithmetic) {
        err = "guetzli: arithmetic-coded input not supported";
        return false;
    }

    JPEGData jpg;
    if (!ReadJpeg(src.data(), src.size(), JPEG_READ_ALL, &jpg)) {
        err = "guetzli: cannot parse source JPEG";
        return false;
    }

    out.clear();
    JPEGOutput hook(append_to_vector, &out);
    if (!WriteJpeg(jpg, strip_metadata, hook)) {
        err = "guetzli: write failed";
        return false;
    }
    if (out.empty()) {
        err = "guetzli: produced no data";
        return false;
    }
    return true;
}
