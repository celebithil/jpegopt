// SPDX-License-Identifier: Apache-2.0

#include "verify.h"

#include <cstdlib>
#include <cstring>

#include "turbojpeg.h"

namespace {

// Maps a TurboJPEG colours space onto the number of interleaved channels we
// decode into. CMYK is decoded as 4 channels; everything else we ask for lands
// in the source's own model (gray stays gray, YCbCr becomes RGB).
int channels_for_colorspace(int colorspace) {
    switch (colorspace) {
        case TJCS_GRAY:
            return 1;
        case TJCS_CMYK:
        case TJCS_YCCK:
            return 4;
        default:
            return 3;
    }
}

int pixel_format_for_colorspace(int colorspace) {
    switch (colorspace) {
        case TJCS_GRAY:
            return TJPF_GRAY;
        case TJCS_CMYK:
        case TJCS_YCCK:
            return TJPF_CMYK;
        default:
            return TJPF_RGB;
    }
}

std::string tj_error(tjhandle handle) {
    const char* s = tj3GetErrorStr(handle);
    return s ? std::string(s) : std::string("unknown error");
}

}  // namespace

bool decode_native(const std::vector<uint8_t>& jpeg, DecodedImage& img, std::string& err) {
    if (jpeg.empty()) {
        err = "empty input";
        return false;
    }

    tjhandle handle = tj3Init(TJINIT_DECOMPRESS);
    if (!handle) {
        err = "tj3Init failed";
        return false;
    }

    if (tj3DecompressHeader(handle, jpeg.data(), jpeg.size()) != 0) {
        err = "header: " + tj_error(handle);
        tj3Destroy(handle);
        return false;
    }

    const int width = tj3Get(handle, TJPARAM_JPEGWIDTH);
    const int height = tj3Get(handle, TJPARAM_JPEGHEIGHT);
    const int precision = tj3Get(handle, TJPARAM_PRECISION);
    const int colorspace = tj3Get(handle, TJPARAM_COLORSPACE);

    if (width <= 0 || height <= 0) {
        err = "invalid dimensions";
        tj3Destroy(handle);
        return false;
    }
    if (precision > 12) {
        err = "unsupported JPEG data precision (16-bit)";
        tj3Destroy(handle);
        return false;
    }

    const int channels = channels_for_colorspace(colorspace);
    const int bps = (precision > 8) ? 2 : 1;
    const int pixfmt = pixel_format_for_colorspace(colorspace);

    const size_t samples = static_cast<size_t>(width) * static_cast<size_t>(height) *
                           static_cast<size_t>(channels);
    const size_t bytes = samples * static_cast<size_t>(bps);

    // tj3Alloc gives us the alignment TurboJPEG expects; the pitch of 0 asks it
    // to use the natural (unpadded) stride for the chosen pixel format.
    unsigned char* buffer = static_cast<unsigned char*>(tj3Alloc(bytes));
    if (!buffer) {
        err = "out of memory";
        tj3Destroy(handle);
        return false;
    }

    int rc;
    if (precision > 8) {
        rc = tj3Decompress12(handle, jpeg.data(), jpeg.size(),
                             reinterpret_cast<short*>(buffer), 0, 0);
    } else {
        rc = tj3Decompress8(handle, jpeg.data(), jpeg.size(), buffer, 0, pixfmt);
    }
    if (rc != 0) {
        err = "decode: " + tj_error(handle);
        tj3Free(buffer);
        tj3Destroy(handle);
        return false;
    }

    img.width = width;
    img.height = height;
    img.channels = channels;
    img.bytes_per_sample = bps;
    img.bit_depth = precision > 0 ? precision : 8;
    img.data.assign(buffer, buffer + bytes);

    tj3Free(buffer);
    tj3Destroy(handle);
    return true;
}

bool same_image(const DecodedImage& a, const DecodedImage& b) {
    return a.width == b.width && a.height == b.height && a.channels == b.channels &&
           a.bytes_per_sample == b.bytes_per_sample && a.bit_depth == b.bit_depth &&
           a.data == b.data;
}
