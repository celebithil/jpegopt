// SPDX-License-Identifier: Apache-2.0

#include "verify.h"

#include <cstdlib>
#include <cstring>

#include "turbojpeg.h"

bool decode_rgb(const std::vector<uint8_t>& jpeg, DecodedImage& img, std::string& err) {
    tjhandle handle = tjInitDecompress();
    if (!handle) {
        err = "tjInitDecompress failed";
        return false;
    }

    int width = 0, height = 0, subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(handle, jpeg.data(), static_cast<unsigned long>(jpeg.size()),
                            &width, &height, &subsamp, &colorspace) != 0) {
        err = std::string("tjDecompressHeader3: ") + tjGetErrorStr();
        tjDestroy(handle);
        return false;
    }

    const int pixel_format = TJPF_RGB;
    const int pitch = width * 3;
    unsigned char* buffer = static_cast<unsigned char*>(tjAlloc(static_cast<size_t>(pitch) * height));
    if (!buffer) {
        err = "out of memory";
        tjDestroy(handle);
        return false;
    }

    if (tjDecompress2(handle, jpeg.data(), static_cast<unsigned long>(jpeg.size()),
                      buffer, width, pitch, height, pixel_format, 0) != 0) {
        err = std::string("tjDecompress2: ") + tjGetErrorStr();
        tjFree(buffer);
        tjDestroy(handle);
        return false;
    }

    img.width = width;
    img.height = height;
    img.rgb.assign(buffer, buffer + static_cast<size_t>(pitch) * height);
    tjFree(buffer);
    tjDestroy(handle);
    return true;
}

bool same_image(const DecodedImage& a, const DecodedImage& b) {
    return a.width == b.width && a.height == b.height && a.rgb == b.rgb;
}
