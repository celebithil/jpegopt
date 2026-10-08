// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A decoded reference image, kept in the source's *native* colour model rather
// than converted to RGB. Comparing in the source's own space is what makes the
// verification meaningful for grayscale, CMYK and 12-bit sources: a conversion
// would normalise away differences the encoder could legitimately introduce.
struct DecodedImage {
    int width = 0;
    int height = 0;
    int channels = 0;        // 1 = grayscale, 3 = RGB/YCbCr, 4 = CMYK
    int bytes_per_sample = 1;  // 1 for 8-bit sources, 2 for 12/16-bit sources
    int bit_depth = 8;         // the source's actual precision (8, 12, ...)
    // Row-major samples, `channels` per pixel, little-endian when
    // bytes_per_sample == 2.
    std::vector<uint8_t> data;

    // Total number of samples (not bytes).
    size_t sample_count() const {
        return static_cast<size_t>(width) * static_cast<size_t>(height) *
               static_cast<size_t>(channels);
    }
};

// Decodes a JPEG into its native colour model and bit depth:
//   - grayscale  -> 1 channel, 8-bit
//   - RGB/YCbCr  -> 3 channels, 8-bit
//   - CMYK       -> 4 channels, 8-bit
//   - 12-bit     -> 3 or 1 channels, 16-bit samples (values 0..4095)
// Returns false with a message when the image cannot be decoded.
bool decode_native(const std::vector<uint8_t>& jpeg, DecodedImage& img, std::string& err);

// True if both images describe the same pixels: identical dimensions, channel
// count and bit depth, and identical sample data.
bool same_image(const DecodedImage& a, const DecodedImage& b);
