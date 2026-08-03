// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;  // 3 bytes per pixel, RGB
};

// Decodes a JPEG to RGB using libjpeg-turbo's TurboJPEG API.
bool decode_rgb(const std::vector<uint8_t>& jpeg, DecodedImage& img, std::string& err);

// True if both decoded buffers are pixel-identical.
bool same_image(const DecodedImage& a, const DecodedImage& b);
