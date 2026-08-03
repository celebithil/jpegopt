// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Re-encodes a JPEG using guetzli's independent reader + writer, which emits a
// sequential JPEG with cost-clustered Huffman tables. APP/COM markers are
// preserved unless strip_metadata is set.
// This is a fully different encoder path from libjpeg-turbo's transcode; the
// output is expected to decode to pixels identical to the input.
// Returns false (with err set) if guetzli cannot parse or write the file.
bool guetzli_encode(const std::vector<uint8_t>& src, bool strip_metadata,
                    std::vector<uint8_t>& out, std::string& err);
